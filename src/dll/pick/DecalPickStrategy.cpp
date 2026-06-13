#include "DecalPickStrategy.hpp"

#include <algorithm>
#include <vector>

#include "../decals/DecalRepository.hpp"
#include "../utils/Logger.h"
#include "GZServPtrs.h"
#include "SC4Rect.h"
#include "SC4Vector.h"
#include "cIGZPersistResourceManager.h"
#include "cISC4Lot.h"
#include "cISC4LotConfiguration.h"
#include "cISC4LotManager.h"
#include "cSC4LotConfigurationObject.h"

namespace {
    constexpr float kPickRadiusMeters = 8.0f;
    constexpr float kCellSizeMeters = 16.0f;

    // The decal list and paint pipeline use the zoom-4 FSH variant; lot
    // exemplars may reference any zoom nibble of the same texture.
    uint32_t NormalizeToZoom4(const uint32_t instanceId) {
        return (instanceId & ~0xFu) | 0x4u;
    }

    // The lot's world-space footprint and facing; the frame for converting
    // between world space and lot-config space.
    struct LotFrame {
        float worldMinX = 0.0f;
        float worldMinZ = 0.0f;
        float worldSizeX = 0.0f;
        float worldSizeZ = 0.0f;
        int32_t facing = 0;
    };

    bool GetLotFrame(const cISC4Lot& lot, LotFrame& out) {
        SC4Rect<int32_t> cellRect;
        if (!lot.GetBoundingRect(cellRect)) {
            return false;
        }
        out.worldMinX = static_cast<float>(cellRect.topLeftX) * kCellSizeMeters;
        out.worldMinZ = static_cast<float>(cellRect.topLeftY) * kCellSizeMeters;
        out.worldSizeX = static_cast<float>(cellRect.bottomRightX - cellRect.topLeftX + 1) * kCellSizeMeters;
        out.worldSizeZ = static_cast<float>(cellRect.bottomRightY - cellRect.topLeftY + 1) * kCellSizeMeters;
        // Lot-config space is rotated 180 degrees from world space at facing 0;
        // verified in-game by matching lot prop world positions against their
        // lot-config coordinates (facing 1 -> mapping 3, facing 2 -> mapping 0).
        out.facing = (lot.GetFacing() + 2) & 3;
        return true;
    }

    // Maps a world position to lot-config space, undoing the lot's facing
    // rotation. Assumes facing steps rotate the lot clockwise (viewed from
    // above, +x east / +z south) with facing 0 leaving lot axes aligned to
    // world axes; if picks land on mirrored spots of rotated lots in-game,
    // swap the cases for facing 1 and 3 here and in LotLocalToWorld.
    bool WorldToLotLocal(const LotFrame& frame, const cS3DVector3& world, float& lotX, float& lotZ) {
        const float dx = world.fX - frame.worldMinX;
        const float dz = world.fZ - frame.worldMinZ;
        if (dx < 0.0f || dz < 0.0f || dx > frame.worldSizeX || dz > frame.worldSizeZ) {
            return false;
        }

        switch (frame.facing) {
        case 0: lotX = dx;                    lotZ = dz;                    break;
        case 1: lotX = dz;                    lotZ = frame.worldSizeX - dx; break;
        case 2: lotX = frame.worldSizeX - dx; lotZ = frame.worldSizeZ - dz; break;
        case 3: lotX = frame.worldSizeZ - dz; lotZ = dx;                    break;
        default: return false;
        }
        return true;
    }

    // Inverse of WorldToLotLocal.
    void LotLocalToWorld(const LotFrame& frame, const float lotX, const float lotZ,
                         float& worldX, float& worldZ) {
        float dx = 0.0f;
        float dz = 0.0f;
        switch (frame.facing) {
        case 0: dx = lotX;                    dz = lotZ;                    break;
        case 1: dx = frame.worldSizeX - lotZ; dz = lotX;                    break;
        case 2: dx = frame.worldSizeX - lotX; dz = frame.worldSizeZ - lotZ; break;
        case 3: dx = lotZ;                    dz = frame.worldSizeZ - lotX; break;
        default: break;
        }
        worldX = frame.worldMinX + dx;
        worldZ = frame.worldMinZ + dz;
    }
}

DecalPickStrategy::DecalPickStrategy(cIGZTerrainDecalService* decalService,
                                     const DecalRepository* decalRepository)
    : decalService_(decalService), decalRepository_(decalRepository) {}

DecalPickStrategy::~DecalPickStrategy() {
    ClearHover();
}

ScenePickMode DecalPickStrategy::Mode() const {
    return ScenePickMode::Decal;
}

float DecalPickStrategy::PickRadiusMeters() const {
    return kPickRadiusMeters;
}

std::optional<ScenePickResult> DecalPickStrategy::Pick(const ScenePickContext& context) {
    const std::vector<Candidate> candidates = CollectCandidates_(context);
    candidateCount_ = static_cast<uint32_t>(candidates.size());

    if (candidates.empty()) {
        lastCandidateKeys_.clear();
        cycleOffset_ = 0;
        return std::nullopt;
    }

    // Reset the cycle offset when the stack under the cursor changes. Keys are
    // compared order-insensitively so small cursor moves that only reorder
    // decal distances keep the current selection depth.
    std::vector<uint64_t> keys;
    keys.reserve(candidates.size());
    for (const Candidate& candidate : candidates) {
        keys.push_back(candidate.key);
    }
    std::sort(keys.begin(), keys.end());
    if (keys != lastCandidateKeys_) {
        cycleOffset_ = 0;
        lastCandidateKeys_ = std::move(keys);
    }

    return ScenePickResult{candidates[CandidateIndex()].picked};
}

uint32_t DecalPickStrategy::CandidateCount() const {
    return candidateCount_;
}

uint32_t DecalPickStrategy::CandidateIndex() const {
    if (candidateCount_ == 0) {
        return 0;
    }
    const auto count = static_cast<int32_t>(candidateCount_);
    return static_cast<uint32_t>(((cycleOffset_ % count) + count) % count);
}

void DecalPickStrategy::CycleCandidates(const int32_t delta) {
    cycleOffset_ += delta;
}

std::vector<DecalPickStrategy::Candidate> DecalPickStrategy::CollectCandidates_(
    const ScenePickContext& context) const {
    std::vector<Candidate> candidates;
    AppendDecalCandidates_(context, candidates);
    AppendLotTextureCandidates_(context, candidates);
    return candidates;
}

void DecalPickStrategy::SetHover(const std::optional<ScenePickResult>& result) {
    TerrainDecalId newId{};
    if (result) {
        if (const auto* decal = std::get_if<PickedDecal>(&*result);
            decal != nullptr && decal->source == PickedDecalSource::Decal) {
            newId.value = decal->decalId;
        }
    }
    SetHoveredDecal_(newId);
}

void DecalPickStrategy::ClearHover() {
    SetHoveredDecal_(TerrainDecalId{});
}

void DecalPickStrategy::AppendDecalCandidates_(const ScenePickContext& context,
                                               std::vector<Candidate>& candidates) const {
    if (!decalService_) {
        return;
    }

    const uint32_t count = decalService_->GetDecalCount();
    if (count == 0) {
        return;
    }

    std::vector<TerrainDecalSnapshot> snapshots(count);
    const uint32_t copied = decalService_->CopyDecals(
        snapshots.data(),
        count,
        static_cast<uint32_t>(sizeof(TerrainDecalSnapshot)));

    struct DecalHit {
        const TerrainDecalSnapshot* snapshot;
        float distSq;
    };
    std::vector<DecalHit> hits;
    constexpr float radiusSq = kPickRadiusMeters * kPickRadiusMeters;

    for (uint32_t i = 0; i < copied; ++i) {
        if (snapshots[i].state.textureKey.instance == 0) {
            continue;
        }
        const cS3DVector2& center = snapshots[i].state.decalInfo.center;
        const float dx = center.fX - context.cursorWorld.fX;
        const float dz = center.fY - context.cursorWorld.fZ;
        const float distSq = dx * dx + dz * dz;
        if (distSq < radiusSq) {
            hits.push_back({&snapshots[i], distSq});
        }
    }

    std::sort(hits.begin(), hits.end(),
              [](const DecalHit& a, const DecalHit& b) { return a.distSq < b.distSq; });

    for (const DecalHit& hit : hits) {
        Candidate candidate;
        candidate.picked.instanceId = hit.snapshot->state.textureKey.instance;
        candidate.picked.decalId = hit.snapshot->id.value;
        candidate.picked.source = PickedDecalSource::Decal;
        candidate.picked.position = cS3DVector3(hit.snapshot->state.decalInfo.center.fX, context.cursorWorld.fY,
                                                hit.snapshot->state.decalInfo.center.fY);
        candidate.key = hit.snapshot->id.value;
        candidates.push_back(candidate);
    }
}

void DecalPickStrategy::AppendLotTextureCandidates_(const ScenePickContext& context,
                                                    std::vector<Candidate>& candidates) const {
    if (!context.city) {
        return;
    }

    cISC4LotManager* lotManager = context.city->GetLotManager();
    if (!lotManager) {
        return;
    }

    cISC4Lot* lot = lotManager->GetLot(context.cursorWorld);
    if (!lot) {
        return;
    }

    cISC4LotConfiguration* config = lot->GetLotConfiguration();
    if (!config) {
        return;
    }

    LotFrame frame;
    if (!GetLotFrame(*lot, frame)) {
        return;
    }

    float lotX = 0.0f;
    float lotZ = 0.0f;
    if (!WorldToLotLocal(frame, context.cursorWorld, lotX, lotZ)) {
        return;
    }

    SC4Vector<cSC4LotConfigurationObject>* objects = config->GetLotConfigurationObjectArray();
    if (!objects) {
        return;
    }

    // Order the texture objects covering the cursor overlays before bases,
    // then smallest rect first; on equal area put the later object (drawn
    // later) first.
    struct TextureHit {
        const cSC4LotConfigurationObject* object;
        uint32_t instanceId;
        decals::TextureKind kind;
        float area;
        size_t index;
    };
    std::vector<TextureHit> hits;

    size_t index = 0;
    for (const cSC4LotConfigurationObject& object : *objects) {
        const size_t objectIndex = index++;
        if (object.type != cSC4LotConfigurationObject::Type::Texture || object.data.empty()) {
            continue;
        }
        if (lotX < object.rectMinX || lotX > object.rectMaxX ||
            lotZ < object.rectMinZ || lotZ > object.rectMaxZ) {
            continue;
        }
        const float area = (object.rectMaxX - object.rectMinX) * (object.rectMaxZ - object.rectMinZ);
        const uint32_t instanceId = ResolveTextureInstance_(object.data[0]);
        hits.push_back({&object, instanceId, ClassifyTexture_(instanceId), area, objectIndex});
    }

    std::sort(hits.begin(), hits.end(), [](const TextureHit& a, const TextureHit& b) {
        const bool aBase = a.kind == decals::TextureKind::Base;
        const bool bBase = b.kind == decals::TextureKind::Base;
        if (aBase != bBase) {
            return bBase;
        }
        if (a.area != b.area) {
            return a.area < b.area;
        }
        return a.index > b.index;
    });

    for (const TextureHit& hit : hits) {
        Candidate candidate;
        candidate.picked.instanceId = hit.instanceId;
        candidate.picked.source = hit.kind == decals::TextureKind::Base
            ? PickedDecalSource::LotBaseTexture
            : PickedDecalSource::LotOverlayTexture;
        candidate.picked.position = context.cursorWorld;

        // World-space footprint of the texture rect for the overlay highlight;
        // axis-aligned in world space since lots rotate in 90-degree steps.
        float ax = 0.0f;
        float az = 0.0f;
        float bx = 0.0f;
        float bz = 0.0f;
        LotLocalToWorld(frame, hit.object->rectMinX, hit.object->rectMinZ, ax, az);
        LotLocalToWorld(frame, hit.object->rectMaxX, hit.object->rectMaxZ, bx, bz);
        candidate.picked.hasWorldRect = true;
        candidate.picked.worldMinX = std::min(ax, bx);
        candidate.picked.worldMaxX = std::max(ax, bx);
        candidate.picked.worldMinZ = std::min(az, bz);
        candidate.picked.worldMaxZ = std::max(az, bz);

        candidate.key = (1ull << 32) | hit.object->objectID;
        candidates.push_back(candidate);
    }

    if (!hits.empty()) {
        LOG_DEBUG("DecalPickStrategy: {} lot texture(s) at lot-local ({:.1f}, {:.1f}), facing {}",
                  hits.size(), lotX, lotZ, frame.facing);
    }
}

uint32_t DecalPickStrategy::ResolveTextureInstance_(const uint32_t rawInstanceId) const {
    const uint32_t normalized = NormalizeToZoom4(rawInstanceId);
    if (decalRepository_ == nullptr || decalRepository_->Contains(normalized)) {
        return normalized;
    }
    if (decalRepository_->Contains(rawInstanceId)) {
        return rawInstanceId;
    }
    LOG_DEBUG("DecalPickStrategy: lot texture 0x{:08X} (zoom-4 0x{:08X}) not in decal repository",
              rawInstanceId, normalized);
    return normalized;
}

decals::TextureKind DecalPickStrategy::ClassifyTexture_(const uint32_t instanceId) const {
    const auto it = textureKindCache_.find(instanceId);
    if (it != textureKindCache_.end()) {
        return it->second;
    }

    cIGZPersistResourceManagerPtr pRM;
    const decals::TextureKind kind =
        decals::ClassifyDecalTexture(static_cast<cIGZPersistResourceManager*>(pRM), instanceId);
    textureKindCache_.emplace(instanceId, kind);
    return kind;
}

void DecalPickStrategy::SetHoveredDecal_(const TerrainDecalId id) {
    if (hasHoveredDecal_ && hoveredDecalId_.value == id.value) {
        return;
    }

    if (hasHoveredDecal_ && decalService_) {
        TerrainDecalSnapshot snap{};
        if (decalService_->GetDecal(hoveredDecalId_, &snap, static_cast<uint32_t>(sizeof(snap)))) {
            snap.state.drawMode = hoveredOriginalDrawMode_;
            decalService_->ReplaceDecal(hoveredDecalId_, &snap.state, static_cast<uint32_t>(sizeof(snap.state)));
        }
    }

    hasHoveredDecal_ = (id.value != 0);
    hoveredDecalId_ = id;
    hoveredOriginalDrawMode_ = 0;

    if (hasHoveredDecal_ && decalService_) {
        TerrainDecalSnapshot snap{};
        if (decalService_->GetDecal(id, &snap, static_cast<uint32_t>(sizeof(snap)))) {
            hoveredOriginalDrawMode_ = snap.state.drawMode;
            snap.state.drawMode = 1;
            decalService_->ReplaceDecal(id, &snap.state, static_cast<uint32_t>(sizeof(snap.state)));
        }
    }
}
