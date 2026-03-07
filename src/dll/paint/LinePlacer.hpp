#pragma once

#include <cstddef>
#include <vector>

#include "PaintPlacement.hpp"

class cISTETerrain;
class WeightedPicker;

class LinePlacer {
public:
    static std::vector<PlannedPaint> ComputePlacements(
        const std::vector<cS3DVector3>& linePoints,
        float spacingMeters,
        int32_t baseRotation,
        bool alignToPath,
        bool randomRotation,
        float randomOffset,
        cISTETerrain* terrain,
        uint32_t seed,
        WeightedPicker* picker = nullptr,
        uint32_t singlePropID = 0,
        size_t maxPlacements = static_cast<size_t>(-1));
};
