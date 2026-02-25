#pragma once

#include <cstddef>
#include <vector>

#include "PropPaintPlacement.hpp"

class cISTETerrain;
class WeightedPropPicker;

class PropLinePlacer {
public:
    static std::vector<PlannedProp> ComputePlacements(
        const std::vector<cS3DVector3>& linePoints,
        float spacingMeters,
        int32_t baseRotation,
        bool alignToPath,
        float randomOffset,
        cISTETerrain* terrain,
        uint32_t seed,
        WeightedPropPicker* picker = nullptr,
        uint32_t singlePropID = 0,
        size_t maxPlacements = static_cast<size_t>(-1));
};
