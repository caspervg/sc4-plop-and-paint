#include "FloraPickStrategy.hpp"

#include <limits>

#include "SC4List.h"
#include "cISC4FloraOccupant.h"
#include "cISC4OccupantManager.h"
#include "cSC4BaseOccupantFilter.h"

namespace {
    constexpr float kPickRadiusMeters = 3.0f;
    constexpr uint32_t kHoverHighlight = 0x9u;
    constexpr uint32_t kFloraOccupantType = 0x74758926u;
    constexpr uint32_t kMaxOccupantQueryCount = std::numeric_limits<uint32_t>::max();

    class FloraOccupantFilter final : public cSC4BaseOccupantFilter {
    public:
        bool IsOccupantTypeIncluded(const uint32_t type) override {
            return type == kFloraOccupantType;
        }
    };
}

FloraPickStrategy::FloraPickStrategy() = default;

FloraPickStrategy::~FloraPickStrategy() {
    ClearHover();
}

ScenePickMode FloraPickStrategy::Mode() const {
    return ScenePickMode::Flora;
}

float FloraPickStrategy::PickRadiusMeters() const {
    return kPickRadiusMeters;
}

std::optional<ScenePickResult> FloraPickStrategy::Pick(const ScenePickContext& context) {
    if (auto picked = PickNearestFlora_(context)) {
        return ScenePickResult{*picked};
    }
    return std::nullopt;
}

void FloraPickStrategy::SetHover(const std::optional<ScenePickResult>& result) {
    cISC4Occupant* newOccupant = nullptr;
    if (result) {
        newOccupant = OccupantFromResult_(*result);
    }

    if (hoveredOccupant_ == newOccupant) {
        return;
    }

    ClearHover();

    if (newOccupant) {
        newOccupant->AddRef();
        hoveredOccupant_ = cRZAutoRefCount<cISC4Occupant>(newOccupant);
        newOccupant->SetHighlight(kHoverHighlight, true);
    }
}

void FloraPickStrategy::ClearHover() {
    if (hoveredOccupant_) {
        hoveredOccupant_->SetHighlight(0x0, true);
        hoveredOccupant_.Reset();
    }
}

std::optional<PickedFlora> FloraPickStrategy::PickNearestFlora_(const ScenePickContext& context) const {
    if (!context.city) {
        return std::nullopt;
    }

    cISC4OccupantManager* occupantManager = context.city->GetOccupantManager();
    if (!occupantManager) {
        return std::nullopt;
    }

    float minBounds[3] = {
        context.cursorWorld.fX - kPickRadiusMeters,
        -std::numeric_limits<float>::max(),
        context.cursorWorld.fZ - kPickRadiusMeters,
    };
    float maxBounds[3] = {
        context.cursorWorld.fX + kPickRadiusMeters,
        std::numeric_limits<float>::max(),
        context.cursorWorld.fZ + kPickRadiusMeters,
    };

    SC4List<cISC4Occupant*> candidates;
    FloraOccupantFilter floraFilter;
    if (!occupantManager->GetOccupantsByBBox(
            candidates,
            minBounds,
            maxBounds,
            &floraFilter,
            kMaxOccupantQueryCount)) {
        return std::nullopt;
    }

    cISC4Occupant* nearest = nullptr;
    cS3DVector3 nearestPosition{};
    uint32_t nearestFloraType = 0;
    int32_t nearestOrientation = 0;
    float nearestDistSq = std::numeric_limits<float>::max();

    for (cISC4Occupant* occupant : candidates) {
        if (!occupant) {
            continue;
        }

        cISC4FloraOccupant* floraOccupant = nullptr;
        if (!occupant->QueryInterface(GZIID_cISC4FloraOccupant, reinterpret_cast<void**>(&floraOccupant)) ||
            !floraOccupant) {
            continue;
        }
        cRZAutoRefCount<cISC4FloraOccupant> floraRef(floraOccupant);

        cS3DVector3 pos{};
        if (!occupant->GetPosition(&pos)) {
            continue;
        }

        const float dx = pos.fX - context.cursorWorld.fX;
        const float dz = pos.fZ - context.cursorWorld.fZ;
        const float distSq = dx * dx + dz * dz;
        if (distSq < nearestDistSq) {
            nearestDistSq = distSq;
            nearest = occupant;
            nearestPosition = pos;
            nearestFloraType = floraRef->GetFloraType();
            nearestOrientation = static_cast<int32_t>(floraRef->GetOrientation());
        }
    }

    if (!nearest || nearestDistSq > (kPickRadiusMeters * kPickRadiusMeters)) {
        occupantManager->ReleaseOccupantList(candidates);
        return std::nullopt;
    }

    nearest->AddRef();
    occupantManager->ReleaseOccupantList(candidates);

    PickedFlora result;
    result.occupant = cRZAutoRefCount<cISC4Occupant>(nearest);
    result.position = nearestPosition;
    result.floraType = nearestFloraType;
    result.orientation = nearestOrientation;
    return result;
}

cISC4Occupant* FloraPickStrategy::OccupantFromResult_(const ScenePickResult& result) {
    if (const auto* flora = std::get_if<PickedFlora>(&result)) {
        return flora->occupant;
    }
    return nullptr;
}
