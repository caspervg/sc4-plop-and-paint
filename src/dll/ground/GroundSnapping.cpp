#include "GroundSnapping.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Ground {
    namespace {
        float Distance(const Vec2 a, const Vec2 b) {
            return std::hypot(a.x - b.x, a.z - b.z);
        }

        // Rings with more points than this are thinned before their sides are added.
        constexpr size_t kThinAbove = 200;
        constexpr float kThinTolerance = 0.05f;
        // Rings with at most this many corners add their corners and the middles of their sides.
        constexpr size_t kCornersUpTo = 32;
    }

    void SnapIndex::Clear() {
        points_.clear();
        segments_.clear();
        buckets_.clear();
    }

    int32_t SnapIndex::Cell(const float coordinate) {
        return static_cast<int32_t>(std::floor(coordinate / kBucket));
    }

    int64_t SnapIndex::Key(const int32_t x, const int32_t z) {
        return (static_cast<int64_t>(x) << 32) ^ static_cast<int64_t>(static_cast<uint32_t>(z));
    }

    void SnapIndex::AddPoint(const Vec2 point) {
        if (!std::isfinite(point.x) || !std::isfinite(point.z)) {
            return;
        }
        buckets_[Key(Cell(point.x), Cell(point.z))].push_back(static_cast<uint32_t>(points_.size()));
        points_.push_back({point, false, {}});
    }

    void SnapIndex::AddSegment(const Vec2 from, const Vec2 to, const bool withMiddle) {
        if (!std::isfinite(from.x) || !std::isfinite(from.z) || !std::isfinite(to.x) || !std::isfinite(to.z)) {
            return;
        }
        const float length = Distance(from, to);
        if (!(length > 1.0e-4f)) {
            return;
        }
        const auto index = static_cast<uint32_t>(segments_.size());
        segments_.push_back({from, to});
        // In the bucket of a sample every half bucket along it: a query that reaches a quarter
        // bucket further than its tolerance then finds every side that comes within it.
        const int samples = static_cast<int>(std::ceil(length / (kBucket * 0.5f)));
        int64_t lastKey = 0;
        for (int i = 0; i <= samples; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(samples);
            const int64_t key = Key(Cell(from.x + (to.x - from.x) * t), Cell(from.z + (to.z - from.z) * t));
            if (i > 0 && key == lastKey) {
                continue;
            }
            lastKey = key;
            std::vector<uint32_t>& bucket = buckets_[key];
            if (bucket.empty() || bucket.back() != ~index) {
                bucket.push_back(~index);
            }
        }
        if (withMiddle) {
            const Vec2 middle{(from.x + to.x) * 0.5f, (from.z + to.z) * 0.5f};
            buckets_[Key(Cell(middle.x), Cell(middle.z))].push_back(static_cast<uint32_t>(points_.size()));
            points_.push_back({middle, true, {from, to}});
        }
    }

    void SnapIndex::AddArea(const Area& area) {
        for (const Ring& ring : area) {
            if (ring.size() < 2) {
                continue;
            }
            const Ring thinned = ring.size() > kThinAbove ? SimplifyPath(ring, kThinTolerance, true) : ring;
            const bool corners = thinned.size() <= kCornersUpTo;
            for (size_t i = 0; i < thinned.size(); ++i) {
                const Vec2 a = thinned[i];
                const Vec2 b = thinned[(i + 1) % thinned.size()];
                if (corners) {
                    AddPoint(a);
                }
                AddSegment(a, b, corners);
            }
        }
    }

    std::optional<SnapResult> SnapIndex::Snap(const Vec2 p, const float tolerance) const {
        if (Empty() || !(tolerance > 0.0f)) {
            return std::nullopt;
        }
        const float reach = tolerance + kBucket * 0.25f;
        const int32_t x0 = Cell(p.x - reach);
        const int32_t x1 = Cell(p.x + reach);
        const int32_t z0 = Cell(p.z - reach);
        const int32_t z1 = Cell(p.z + reach);

        // Corners win over middles, middles over edges.
        const PointEntry* bestCorner = nullptr;
        float bestCornerDistance = tolerance;
        const PointEntry* bestMiddle = nullptr;
        float bestMiddleDistance = tolerance;
        const Segment* bestEdge = nullptr;
        Vec2 bestEdgePoint{};
        float bestEdgeDistance = tolerance;
        for (int32_t x = x0; x <= x1; ++x) {
            for (int32_t z = z0; z <= z1; ++z) {
                const auto bucket = buckets_.find(Key(x, z));
                if (bucket == buckets_.end()) {
                    continue;
                }
                for (const uint32_t entry : bucket->second) {
                    if (static_cast<int32_t>(entry) >= 0) {
                        const PointEntry& candidate = points_[entry];
                        const float d = Distance(candidate.point, p);
                        const PointEntry*& best = candidate.middle ? bestMiddle : bestCorner;
                        float& bestDistance = candidate.middle ? bestMiddleDistance : bestCornerDistance;
                        if (d <= bestDistance) {
                            best = &candidate;
                            bestDistance = d;
                        }
                        continue;
                    }
                    const Segment& segment = segments_[~entry];
                    const Vec2 nearest = NearestOnSegment(p, segment.from, segment.to);
                    const float d = Distance(nearest, p);
                    if (d < bestEdgeDistance) {
                        bestEdge = &segment;
                        bestEdgePoint = nearest;
                        bestEdgeDistance = d;
                    }
                }
            }
        }
        if (bestCorner) {
            return SnapResult{bestCorner->point, SnapKind::Point, {}, {}};
        }
        if (bestMiddle) {
            return SnapResult{bestMiddle->point, SnapKind::Middle, bestMiddle->edge.from, bestMiddle->edge.to};
        }
        if (bestEdge) {
            return SnapResult{bestEdgePoint, SnapKind::Edge, bestEdge->from, bestEdge->to};
        }
        return std::nullopt;
    }

    Vec2 SnapToAngle(const Vec2 from, const Vec2 to, const float stepRadians) {
        const float length = Distance(from, to);
        if (!(length > 0.0f) || !(stepRadians > 0.0f)) {
            return to;
        }
        const float angle = std::round(std::atan2(to.z - from.z, to.x - from.x) / stepRadians) * stepRadians;
        // Keeps the distance along the snapped ray, not the cursor's distance.
        const Vec2 direction{std::cos(angle), std::sin(angle)};
        const float along = std::max(0.0f, (to.x - from.x) * direction.x + (to.z - from.z) * direction.z);
        return {from.x + direction.x * along, from.z + direction.z * along};
    }

    std::optional<SnapResult> SnapToDirection(const Vec2 from, const Vec2 to, const Vec2 direction,
                                              const float toleranceRadians) {
        const float length = Distance(from, to);
        const float directionLength = std::hypot(direction.x, direction.z);
        if (!(length > 0.0f) || !(directionLength > 0.0f)) {
            return std::nullopt;
        }
        const Vec2 d{direction.x / directionLength, direction.z / directionLength};
        const Vec2 u{(to.x - from.x) / length, (to.z - from.z) / length};
        const float cosine = std::clamp(u.x * d.x + u.z * d.z, -1.0f, 1.0f);
        const float sine = u.x * d.z - u.z * d.x;
        const float limit = std::sin(toleranceRadians);
        if (std::abs(sine) <= limit) {
            const float along = (to.x - from.x) * d.x + (to.z - from.z) * d.z;
            return SnapResult{{from.x + d.x * along, from.z + d.z * along}, SnapKind::Parallel, {}, {}};
        }
        if (std::abs(cosine) <= limit) {
            const Vec2 square{-d.z, d.x};
            const float along = (to.x - from.x) * square.x + (to.z - from.z) * square.z;
            return SnapResult{{from.x + square.x * along, from.z + square.z * along}, SnapKind::Square, {}, {}};
        }
        return std::nullopt;
    }
}
