#include "PropPickStrategy.hpp"

#include <cmath>
#include <limits>
#include <unordered_set>

#include "../props/PropStripperInputControl.hpp"
#include "../utils/Logger.h"
#include "SC4List.h"
#include "SC4Rect.h"
#include "cISC4PropOccupant.h"

namespace {
    constexpr float kPickRadiusMeters = 3.0f;
    constexpr uint32_t kHoverHighlight = 0x9u;
}

PropPickStrategy::PropPickStrategy(const uint32_t sourceFlags)
    : sourceFlags_(sourceFlags) {}

PropPickStrategy::~PropPickStrategy() {
    ClearHover();
}

ScenePickMode PropPickStrategy::Mode() const {
    return ScenePickMode::Prop;
}

float PropPickStrategy::PickRadiusMeters() const {
    return kPickRadiusMeters;
}

std::optional<ScenePickResult> PropPickStrategy::Pick(const ScenePickContext& context) {
    if (auto picked = PickNearestProp_(context)) {
        return ScenePickResult{*picked};
    }
    return std::nullopt;
}

void PropPickStrategy::SetHover(const std::optional<ScenePickResult>& result) {
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

void PropPickStrategy::ClearHover() {
    if (hoveredOccupant_) {
        hoveredOccupant_->SetHighlight(0x0, true);
        hoveredOccupant_.Reset();
    }
}

bool PropPickStrategy::HasSource_(const PickedPropSource source) const {
    switch (source) {
    case PickedPropSource::City:
        return (sourceFlags_ & PropStripperInputControl::SourceFlagCity) != 0;
    case PickedPropSource::Lot:
        return (sourceFlags_ & PropStripperInputControl::SourceFlagLot) != 0;
    case PickedPropSource::Street:
        return (sourceFlags_ & PropStripperInputControl::SourceFlagStreet) != 0;
    }
    return false;
}

bool PropPickStrategy::TryGetCursorCell_(cISC4City* city,
                                         const cS3DVector3& cursorWorld,
                                         int& cellX,
                                         int& cellZ) {
    if (!city) {
        return false;
    }
    return city->PositionToCell(cursorWorld.fX, cursorWorld.fZ, cellX, cellZ) != 0;
}

void PropPickStrategy::AppendCandidateProps_(std::vector<CollectedProp>& candidates,
                                             cISC4City* city,
                                             cISC4PropManager* propManager,
                                             const cS3DVector3& cursorWorld,
                                             const PickedPropSource source) const {
    if (!propManager || !HasSource_(source)) {
        return;
    }

    SC4List<cISC4Occupant*> rawCandidates;
    switch (source) {
    case PickedPropSource::City: {
        const SC4Rect<float> rect(
            cursorWorld.fX - kPickRadiusMeters,
            cursorWorld.fZ - kPickRadiusMeters,
            cursorWorld.fX + kPickRadiusMeters,
            cursorWorld.fZ + kPickRadiusMeters);
        propManager->GetCityProps(rawCandidates, rect);
        break;
    }
    case PickedPropSource::Lot:
    case PickedPropSource::Street: {
        int cellX = 0;
        int cellZ = 0;
        if (!TryGetCursorCell_(city, cursorWorld, cellX, cellZ)) {
            return;
        }
        if (source == PickedPropSource::Lot) {
            propManager->GetLotProps(rawCandidates, cellX, cellZ);
        }
        else {
            propManager->GetStreetProps(rawCandidates, cellX, cellZ);
        }
        break;
    }
    }

    for (cISC4Occupant* occupant : rawCandidates) {
        if (!occupant) {
            continue;
        }

        cISC4PropOccupant* propOccupant = nullptr;
        if (!occupant->QueryInterface(GZIID_cISC4PropOccupant, reinterpret_cast<void**>(&propOccupant)) ||
            !propOccupant) {
            continue;
        }

        cS3DVector3 pos{};
        if (!occupant->GetPosition(&pos)) {
            propOccupant->Release();
            continue;
        }

        candidates.push_back({
            .occupant = occupant,
            .source = source,
            .position = pos,
            .propType = propOccupant->GetPropType(),
            .orientation = propOccupant->GetOrientation(),
        });
        propOccupant->Release();
    }
}

std::optional<PickedProp> PropPickStrategy::PickNearestProp_(const ScenePickContext& context) const {
    if (!context.city) {
        return std::nullopt;
    }

    cISC4PropManager* propManager = context.city->GetPropManager();
    if (!propManager) {
        return std::nullopt;
    }
    std::vector<CollectedProp> candidates;
    AppendCandidateProps_(candidates, context.city, propManager, context.cursorWorld, PickedPropSource::City);
    AppendCandidateProps_(candidates, context.city, propManager, context.cursorWorld, PickedPropSource::Lot);
    AppendCandidateProps_(candidates, context.city, propManager, context.cursorWorld, PickedPropSource::Street);

    std::unordered_set<cISC4Occupant*> seen;
    auto it = candidates.begin();
    while (it != candidates.end()) {
        if (!it->occupant || !seen.insert(it->occupant).second) {
            it = candidates.erase(it);
        }
        else {
            ++it;
        }
    }

    const CollectedProp* nearest = nullptr;
    float nearestDistSq = std::numeric_limits<float>::max();
    for (const auto& candidate : candidates) {
        const float dx = candidate.position.fX - context.cursorWorld.fX;
        const float dz = candidate.position.fZ - context.cursorWorld.fZ;
        const float distSq = dx * dx + dz * dz;
        if (distSq < nearestDistSq) {
            nearestDistSq = distSq;
            nearest = &candidate;
        }
    }

    if (!nearest || nearestDistSq > (kPickRadiusMeters * kPickRadiusMeters)) {
        return std::nullopt;
    }

    nearest->occupant->AddRef();
    PickedProp result;
    result.occupant = cRZAutoRefCount<cISC4Occupant>(nearest->occupant);
    result.source = nearest->source;
    result.position = nearest->position;
    result.propType = nearest->propType;
    result.orientation = nearest->orientation;
    return result;
}

cISC4Occupant* PropPickStrategy::OccupantFromResult_(const ScenePickResult& result) {
    if (const auto* prop = std::get_if<PickedProp>(&result)) {
        return prop->occupant;
    }
    return nullptr;
}
