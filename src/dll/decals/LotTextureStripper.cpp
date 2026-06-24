#include "LotTextureStripper.hpp"

#include "cISC4City.h"
#include "cISC4Lot.h"
#include "cISC4LotBaseTextureOccupant.h"
#include "cISC4LotDeveloper.h"
#include "cISC4LotManager.h"
#include "cS3DVector3.h"
#include "../utils/Logger.h"

namespace lottex {

namespace {
    // Cell size in meters; spec cellX/cellY are global city-grid coordinates.
    constexpr float kCellSizeMeters = 16.0f;
    // The occupant stores its std::vector<LotBaseTextureSpecification> at
    // this+0x54 (begin) / +0x58 (end). RE: docs/lot-texture-stripping.md.
    constexpr size_t kSpecVectorOffset = 0x54;

    // The lot under the cursor plus the developer that owns its textures.
    struct LotContext {
        cISC4LotDeveloper* developer{nullptr};
        cISC4Lot* lot{nullptr};
    };

    bool GetLotContext(cISC4City* city, float worldX, float worldZ, LotContext& out) {
        if (!city) {
            return false;
        }
        cISC4LotManager* lotManager = city->GetLotManager();
        out.developer = city->GetLotDeveloper();
        if (!lotManager || !out.developer) {
            return false;
        }
        out.lot = lotManager->GetLot(cS3DVector3(worldX, 0.0f, worldZ));
        return out.lot != nullptr;
    }

    // Returns the lot texture occupant for `lot`, AddRef'd (caller must Release),
    // or nullptr. `create` spawns one if missing (needed for undo after a full
    // clear may have torn the occupant down).
    cISC4LotBaseTextureOccupant* AcquireOccupant(const LotContext& ctx, bool create) {
        cISC4LotBaseTextureOccupant* occupant = nullptr;
        if (!ctx.developer->GetLotBaseTextureOccupant(ctx.lot, occupant, create) || !occupant) {
            return nullptr;
        }
        return occupant;
    }

    std::vector<LotTextureSpec> ReadSpecs(cISC4LotBaseTextureOccupant* occupant) {
        std::vector<LotTextureSpec> specs;
        auto* base = reinterpret_cast<uint8_t*>(occupant);
        auto* begin = *reinterpret_cast<LotTextureSpec**>(base + kSpecVectorOffset);
        auto* end = *reinterpret_cast<LotTextureSpec**>(base + kSpecVectorOffset + sizeof(void*));
        if (!begin || !end || end < begin) {
            return specs;
        }
        const size_t count = static_cast<size_t>(end - begin);
        if (count > 4096) {
            // Bogus count: spec vector layout did not match (should not happen).
            LOG_WARN("LotTextureStripper: spec count {} looks invalid; aborting read", count);
            return specs;
        }
        specs.assign(begin, end);
        return specs;
    }

    bool WriteSpecs(cISC4LotBaseTextureOccupant* occupant, const std::vector<LotTextureSpec>& specs) {
        // SetTextureSpecification ignores a count of 0 (early return in the
        // engine), so it cannot clear the last texture this way; callers should
        // route a full clear through SetEmptyLotBaseTexture instead.
        if (specs.empty()) {
            return false;
        }
        const auto* raw =
            reinterpret_cast<const cISC4LotBaseTextureOccupant::LotBaseTextureSpecification*>(specs.data());
        return occupant->SetTextureSpecification(raw, static_cast<uint32_t>(specs.size()), nullptr);
    }

    bool CellCenterInRect(const LotTextureSpec& spec,
                          float minX, float minZ, float maxX, float maxZ) {
        const float cx = spec.cellX * kCellSizeMeters + kCellSizeMeters * 0.5f;
        const float cz = spec.cellY * kCellSizeMeters + kCellSizeMeters * 0.5f;
        return cx >= minX && cx <= maxX && cz >= minZ && cz <= maxZ;
    }
}

bool RemoveLotTexture(cISC4City* city,
                      const float worldX, const float worldZ,
                      const uint32_t normalizedIID,
                      const float worldMinX, const float worldMinZ,
                      const float worldMaxX, const float worldMaxZ,
                      RemovedLotTexture& out) {
    LotContext ctx;
    if (!GetLotContext(city, worldX, worldZ, ctx)) {
        return false;
    }
    cISC4LotBaseTextureOccupant* occupant = AcquireOccupant(ctx, false);
    if (!occupant) {
        return false;
    }

    const std::vector<LotTextureSpec> specs = ReadSpecs(occupant);
    std::vector<LotTextureSpec> kept;
    kept.reserve(specs.size());
    uint32_t removed = 0;

    for (const LotTextureSpec& spec : specs) {
        const bool iidMatch = (spec.iid & ~0xFu) == (normalizedIID & ~0xFu);
        if (iidMatch && CellCenterInRect(spec, worldMinX, worldMinZ, worldMaxX, worldMaxZ)) {
            ++removed;
        }
        else {
            kept.push_back(spec);
        }
    }

    if (removed == 0) {
        occupant->Release();
        return false;
    }

    bool ok;
    if (kept.empty()) {
        // Removing the lot's last texture(s): SetTextureSpecification ignores an
        // empty list (engine early-returns on count 0). Route the full clear
        // through the engine's empty-lot path, which renders the empty-zone
        // texture (terrain/dirt depending on zone type).
        occupant->Release();
        ok = ctx.developer->SetEmptyLotBaseTexture(ctx.lot);
    }
    else {
        ok = WriteSpecs(occupant, kept);
        occupant->Release();
    }
    if (!ok) {
        return false;
    }

    out.worldX = worldX;
    out.worldZ = worldZ;
    out.preEditSpecs = specs;
    out.removedCount = removed;
    LOG_INFO("LotTextureStripper: removed {} spec(s) for texture 0x{:08X}",
             removed, normalizedIID);
    return true;
}

bool RestoreLotTexture(cISC4City* city, const RemovedLotTexture& removed) {
    if (removed.preEditSpecs.empty()) {
        return false;
    }
    LotContext ctx;
    if (!GetLotContext(city, removed.worldX, removed.worldZ, ctx)) {
        return false;
    }
    // create=true: a full clear may have torn down the occupant.
    cISC4LotBaseTextureOccupant* occupant = AcquireOccupant(ctx, true);
    if (!occupant) {
        return false;
    }

    // Replace the current vector wholesale with the captured pre-edit state.
    const bool ok = WriteSpecs(occupant, removed.preEditSpecs);
    occupant->Release();
    if (ok) {
        LOG_INFO("LotTextureStripper: restored {} spec(s)", removed.preEditSpecs.size());
    }
    return ok;
}

}  // namespace lottex
