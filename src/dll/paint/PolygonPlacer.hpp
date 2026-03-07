#pragma once

#include <cstddef>
#include <vector>

#include "PaintPlacement.hpp"

class cISTETerrain;
class WeightedPicker;

class PolygonPlacer {
public:
    static std::vector<PlannedPaint> ComputePlacements(
        const std::vector<cS3DVector3>& polygonVertices,
        float densityPer100Sqm,
        float densityVariation,
        int32_t baseRotation,
        bool randomRotation,
        cISTETerrain* terrain,
        uint32_t seed,
        WeightedPicker* picker = nullptr,
        uint32_t singlePropID = 0,
        size_t maxPlacements = static_cast<size_t>(-1));

private:
    static bool PointInPolygon_(float px, float pz, const std::vector<cS3DVector3>& polygon);
};
