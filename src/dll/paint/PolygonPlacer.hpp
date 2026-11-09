#pragma once

#include <cstddef>
#include <vector>

#include "PaintPlacement.hpp"

class cISTETerrain;
class WeightedPicker;

enum class PolygonHeightSamplingMode {
    Terrain = 0,
    InterpolateVertices = 1
};

struct RowLayout {
    float spacingMeters = 6.0f;  // between the rows
    float stepMeters = 3.0f;     // between the items of a row
    float angleDegrees = 0.0f;   // 0 runs along x
    bool alignToRows = false;
};

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
        PolygonHeightSamplingMode heightSamplingMode = PolygonHeightSamplingMode::Terrain,
        WeightedPicker* picker = nullptr,
        uint32_t singlePropID = 0,
        size_t maxPlacements = static_cast<size_t>(-1),
        float edgeMarginMeters = 0.0f);

    // Straight parallel rows, centred in the polygon.
    static std::vector<PlannedPaint> ComputeRowPlacements(
        const std::vector<cS3DVector3>& polygonVertices,
        const RowLayout& rows,
        float edgeMarginMeters,
        int32_t baseRotation,
        bool randomRotation,
        cISTETerrain* terrain,
        uint32_t seed,
        PolygonHeightSamplingMode heightSamplingMode = PolygonHeightSamplingMode::Terrain,
        WeightedPicker* picker = nullptr,
        uint32_t singlePropID = 0,
        size_t maxPlacements = static_cast<size_t>(-1));

    [[nodiscard]] static bool CanTriangulate(const std::vector<cS3DVector3>& polygonVertices);

private:
    static bool PointInPolygon_(float px, float pz, const std::vector<cS3DVector3>& polygon);
};
