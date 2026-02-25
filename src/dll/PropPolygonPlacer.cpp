#include "PropPolygonPlacer.hpp"

#include <algorithm>
#include <cmath>
#include <random>

#include "WeightedPropPicker.hpp"
#include "cISTETerrain.h"

namespace {
    constexpr float kEpsilon = 1e-6f;
}

std::vector<PlannedProp> PropPolygonPlacer::ComputePlacements(
    const std::vector<cS3DVector3>& polygonVertices,
    const float densityPer100Sqm,
    const int32_t baseRotation,
    const bool randomRotation,
    cISTETerrain* terrain,
    const uint32_t seed,
    WeightedPropPicker* picker,
    const uint32_t singlePropID,
    const size_t maxPlacements) {
    std::vector<PlannedProp> result;
    if (polygonVertices.size() < 3 || densityPer100Sqm <= kEpsilon || maxPlacements == 0) {
        return result;
    }

    float minX = polygonVertices.front().fX;
    float maxX = minX;
    float minZ = polygonVertices.front().fZ;
    float maxZ = minZ;
    for (const auto& v : polygonVertices) {
        minX = std::min(minX, v.fX);
        maxX = std::max(maxX, v.fX);
        minZ = std::min(minZ, v.fZ);
        maxZ = std::max(maxZ, v.fZ);
    }

    const float cellSize = std::sqrt(100.0f / densityPer100Sqm);
    if (cellSize <= kEpsilon) {
        return result;
    }

    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> jitterDist(-0.4f, 0.4f);
    std::uniform_int_distribution<int32_t> rotDist(0, 3);

    for (float x = minX; x <= maxX; x += cellSize) {
        for (float z = minZ; z <= maxZ; z += cellSize) {
            const float px = x + jitterDist(rng) * cellSize;
            const float pz = z + jitterDist(rng) * cellSize;
            if (!PointInPolygon_(px, pz, polygonVertices)) {
                continue;
            }

            float py = 0.0f;
            if (terrain) {
                py = terrain->GetAltitude(px, pz);
            }
            else {
                py = polygonVertices.front().fY;
            }

            const int32_t rotation = randomRotation ? rotDist(rng) : (baseRotation & 3);
            result.push_back({
                cS3DVector3(px, py, pz),
                rotation,
                picker ? picker->Pick() : singlePropID
            });

            if (result.size() >= maxPlacements) {
                return result;
            }
        }
    }

    return result;
}

bool PropPolygonPlacer::PointInPolygon_(const float px, const float pz, const std::vector<cS3DVector3>& polygon) {
    if (polygon.size() < 3) {
        return false;
    }

    bool inside = false;
    const size_t count = polygon.size();
    for (size_t i = 0, j = count - 1; i < count; j = i++) {
        const float zi = polygon[i].fZ;
        const float zj = polygon[j].fZ;
        const float xi = polygon[i].fX;
        const float xj = polygon[j].fX;

        const bool crosses = (zi > pz) != (zj > pz);
        if (!crosses) {
            continue;
        }

        const float denom = zj - zi;
        if (std::abs(denom) <= kEpsilon) {
            continue;
        }

        const float intersectX = (xj - xi) * (pz - zi) / denom + xi;
        if (px < intersectX) {
            inside = !inside;
        }
    }

    return inside;
}
