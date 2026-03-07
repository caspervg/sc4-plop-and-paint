#include "PolygonPlacer.hpp"

#include <algorithm>
#include <cmath>
#include <random>

#include "../common/WeightedPicker.hpp"
#include "cISTETerrain.h"

namespace {
    constexpr float kEpsilon = 1e-6f;

    uint32_t HashPatchCell_(const int x, const int z, const uint32_t seed) {
        uint32_t value = seed;
        value ^= static_cast<uint32_t>(x) * 0x9E3779B9u;
        value ^= static_cast<uint32_t>(z) * 0x85EBCA6Bu;
        value ^= value >> 16;
        value *= 0x7FEB352Du;
        value ^= value >> 15;
        value *= 0x846CA68Bu;
        value ^= value >> 16;
        return value;
    }

    float PatchNoise_(const float x, const float z, const float patchCellSize, const uint32_t seed) {
        if (patchCellSize <= kEpsilon) {
            return 0.5f;
        }

        const int patchX = static_cast<int>(std::floor(x / patchCellSize));
        const int patchZ = static_cast<int>(std::floor(z / patchCellSize));
        return static_cast<float>(HashPatchCell_(patchX, patchZ, seed)) / 4294967295.0f;
    }
}

std::vector<PlannedPaint> PolygonPlacer::ComputePlacements(
    const std::vector<cS3DVector3>& polygonVertices,
    const float densityPer100Sqm,
    const float densityVariation,
    const int32_t baseRotation,
    const bool randomRotation,
    cISTETerrain* terrain,
    const uint32_t seed,
    WeightedPicker* picker,
    const uint32_t singlePropID,
    const size_t maxPlacements) {
    std::vector<PlannedPaint> result;
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
    const float clampedVariation = std::clamp(densityVariation, 0.0f, 1.0f);
    const float patchCellSize = cellSize * (3.0f + 5.0f * clampedVariation);
    const float jitterExtent = 0.4f + 0.2f * clampedVariation;
    std::uniform_real_distribution<float> jitterDist(-jitterExtent, jitterExtent);
    std::uniform_int_distribution<int32_t> rotDist(0, 3);

    for (float x = minX; x <= maxX; x += cellSize) {
        for (float z = minZ; z <= maxZ; z += cellSize) {
            size_t placementAttempts = 1;
            if (clampedVariation > kEpsilon) {
                const float patchNoise = PatchNoise_(x + 0.5f * cellSize, z + 0.5f * cellSize, patchCellSize, seed);
                const float localDensityMultiplier = std::clamp(
                    1.0f + ((patchNoise - 0.5f) * 1.6f * clampedVariation),
                    0.2f,
                    1.8f);
                placementAttempts = static_cast<size_t>(localDensityMultiplier);
                const float fractionalAttempt = localDensityMultiplier - static_cast<float>(placementAttempts);
                std::bernoulli_distribution extraAttemptDist(fractionalAttempt);
                if (extraAttemptDist(rng)) {
                    ++placementAttempts;
                }
            }

            for (size_t attempt = 0; attempt < placementAttempts; ++attempt) {
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
    }

    return result;
}

bool PolygonPlacer::PointInPolygon_(const float px, const float pz, const std::vector<cS3DVector3>& polygon) {
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
