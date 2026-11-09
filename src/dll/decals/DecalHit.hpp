#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

namespace decals {
    inline constexpr float kPickRadiusMeters = 8.0f;

    // How well a point on the ground hits a decal: lower is better, nothing is a miss. Decals with
    // their centre near the point come first, nearest first. Then come decals that the point is
    // only somewhere on, so that a large decal can be hit away from its centre, smallest first.
    template <typename State>
    [[nodiscard]] std::optional<float> MeasureDecalHit(const State& state, const float x, const float z) {
        constexpr float kRadiusSq = kPickRadiusMeters * kPickRadiusMeters;
        constexpr float kTau = 6.28318530717958647692f;

        const float dx = x - state.decalInfo.center.fX;
        const float dz = z - state.decalInfo.center.fY;
        const float distSq = dx * dx + dz * dz;
        if (distSq < kRadiusSq) {
            return distSq;
        }

        const float depth = state.decalInfo.baseSize;
        const float width = depth * std::max(state.decalInfo.aspectMultiplier, 0.1f);
        const float angle = state.decalInfo.rotationTurns * kTau;
        const float localX = dx * std::cos(angle) - dz * std::sin(angle);
        const float localZ = dx * std::sin(angle) + dz * std::cos(angle);
        if (std::abs(localX) <= width * 0.5f && std::abs(localZ) <= depth * 0.5f) {
            return kRadiusSq + width * depth;
        }
        return std::nullopt;
    }
}
