#pragma once

#include <cstddef>
#include <vector>

#include "PropPaintPlacement.hpp"

class cISTETerrain;
class WeightedPropPicker;

class PropPolygonPlacer {
public:
    static std::vector<PlannedProp> ComputePlacements(
        const std::vector<cS3DVector3>& polygonVertices,
        float densityPer100Sqm,
        int32_t baseRotation,
        bool randomRotation,
        cISTETerrain* terrain,
        uint32_t seed,
        WeightedPropPicker* picker = nullptr,
        uint32_t singlePropID = 0,
        size_t maxPlacements = static_cast<size_t>(-1));

private:
    static bool PointInPolygon_(float px, float pz, const std::vector<cS3DVector3>& polygon);
};
