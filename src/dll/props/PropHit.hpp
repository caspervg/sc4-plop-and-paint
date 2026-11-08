#pragma once

#include <cmath>
#include <tuple>

// How well the cursor points at a prop. Lower is better.
struct PropHit {
    float distance = 0.0f; // 0 inside the footprint, otherwise from the cursor to the prop's origin
    float area = 0.0f;     // so a small prop on top of a large one stays reachable

    [[nodiscard]] bool operator<(const PropHit& other) const {
        return std::tie(distance, area) < std::tie(other.distance, other.area);
    }
};

// GetBoundingBox gives the bounds in world coordinates: minimum first, then maximum.
template <typename Occupant, typename Vector>
[[nodiscard]] PropHit MeasurePropHit(Occupant& occupant, const Vector& position, const Vector& cursor) {
    Vector min{};
    Vector max{};
    occupant.GetBoundingBox(&min, &max);

    const bool inside = cursor.fX >= min.fX && cursor.fX <= max.fX && cursor.fZ >= min.fZ && cursor.fZ <= max.fZ;
    return {inside ? 0.0f : std::hypot(position.fX - cursor.fX, position.fZ - cursor.fZ),
            (max.fX - min.fX) * (max.fZ - min.fZ)};
}
