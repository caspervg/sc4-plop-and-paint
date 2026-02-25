#include "PropLinePlacer.hpp"

#include <algorithm>
#include <cmath>
#include <random>

#include "WeightedPropPicker.hpp"
#include "cISTETerrain.h"

namespace {
    constexpr float kEpsilon = 1e-4f;
    constexpr float kPi = 3.14159265358979323846f;

    int32_t QuantizeRotationStep(const float dirX, const float dirZ) {
        const float angle = std::atan2(dirX, dirZ);
        int32_t step = static_cast<int32_t>(std::lround(angle / (kPi * 0.5f)));
        step %= 4;
        if (step < 0) {
            step += 4;
        }
        return step;
    }
}

std::vector<PlannedProp> PropLinePlacer::ComputePlacements(
    const std::vector<cS3DVector3>& linePoints,
    const float spacingMeters,
    const int32_t baseRotation,
    const bool alignToPath,
    const float randomOffset,
    cISTETerrain* terrain,
    const uint32_t seed,
    WeightedPropPicker* picker,
    const uint32_t singlePropID,
    const size_t maxPlacements) {
    std::vector<PlannedProp> result;
    if (linePoints.size() < 2 || spacingMeters <= kEpsilon || maxPlacements == 0) {
        return result;
    }

    const float jitterAmount = std::max(0.0f, randomOffset);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> jitterDist(-jitterAmount, jitterAmount);

    float carry = 0.0f;

    for (size_t i = 1; i < linePoints.size(); ++i) {
        const cS3DVector3& p0 = linePoints[i - 1];
        const cS3DVector3& p1 = linePoints[i];

        const float dx = p1.fX - p0.fX;
        const float dy = p1.fY - p0.fY;
        const float dz = p1.fZ - p0.fZ;
        const float segLen = std::sqrt(dx * dx + dz * dz);
        if (segLen <= kEpsilon) {
            continue;
        }

        const float dirX = dx / segLen;
        const float dirZ = dz / segLen;

        int32_t rotation = baseRotation & 3;
        if (alignToPath) {
            rotation = (rotation + QuantizeRotationStep(dirX, dirZ)) & 3;
        }

        float pos = -carry;
        while (pos < segLen) {
            if (pos >= 0.0f) {
                const float t = pos / segLen;
                float worldX = p0.fX + dx * t;
                float worldY = p0.fY + dy * t;
                float worldZ = p0.fZ + dz * t;

                if (jitterAmount > 0.0f) {
                    const float jitter = jitterDist(rng);
                    worldX += -dirZ * jitter;
                    worldZ += dirX * jitter;
                    if (terrain) {
                        worldY = terrain->GetAltitude(worldX, worldZ);
                    }
                }
                else if (terrain) {
                    worldY = terrain->GetAltitude(worldX, worldZ);
                }

                result.push_back({
                    cS3DVector3(worldX, worldY, worldZ),
                    rotation,
                    picker ? picker->Pick() : singlePropID
                });

                if (result.size() >= maxPlacements) {
                    return result;
                }
            }

            pos += spacingMeters;
        }

        carry = pos - segLen;
    }

    return result;
}
