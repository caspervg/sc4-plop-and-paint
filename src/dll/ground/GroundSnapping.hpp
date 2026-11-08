#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "GroundGeometry.hpp"

// Snapping to what is already painted: its points, the middles of its sides and its edges, and
// lining up with an edge (parallel or square to it) or with 15 degree steps. Plain geometry,
// tested on its own.
//
// The points and edges sit in a grid of buckets, so finding what is near the cursor looks at a
// few buckets however much is painted. The index is made again only when the painting changes.
namespace Ground {
    enum class SnapKind : uint8_t {
        None,
        Point,     // a corner or a clicked point
        Middle,    // the middle of a side
        Edge,      // somewhere on an edge
        Parallel,  // on a line through the last point, parallel to a recent edge
        Square,    // on a line through the last point, square to a recent edge
        Angle,     // on a 15 degree step from the last point
        Grid,
    };

    struct SnapResult {
        Vec2 point{};
        SnapKind kind = SnapKind::None;
        // The edge snapped to, for Edge and Middle, so a later point can line up with it.
        Vec2 edgeFrom{};
        Vec2 edgeTo{};
    };

    class SnapIndex {
    public:
        void Clear();
        [[nodiscard]] bool Empty() const { return points_.empty() && segments_.empty(); }

        void AddPoint(Vec2 point);
        // An edge; withMiddle also makes its middle a point to snap to.
        void AddSegment(Vec2 from, Vec2 to, bool withMiddle);
        // Every side of every ring. Rings with many points (curves, rough edges) are thinned
        // first, and only rings with few corners add their corners and middles.
        void AddArea(const Area& area);

        // The best snap within tolerance of p: a point before a middle before an edge.
        [[nodiscard]] std::optional<SnapResult> Snap(Vec2 p, float tolerance) const;

        [[nodiscard]] size_t PointCount() const { return points_.size(); }
        [[nodiscard]] size_t SegmentCount() const { return segments_.size(); }

    private:
        struct Segment {
            Vec2 from;
            Vec2 to;
        };
        struct PointEntry {
            Vec2 point;
            bool middle;
            Segment edge;
        };

        static constexpr float kBucket = 8.0f;
        [[nodiscard]] static int64_t Key(int32_t x, int32_t z);
        [[nodiscard]] static int32_t Cell(float coordinate);

        std::vector<PointEntry> points_{};
        std::vector<Segment> segments_{};
        // Bucket key to entries: points as their index, segments as ~index.
        std::unordered_map<int64_t, std::vector<uint32_t>> buckets_{};
    };

    // to moved onto the nearest ray from from at a multiple of stepRadians to the x axis.
    [[nodiscard]] Vec2 SnapToAngle(Vec2 from, Vec2 to, float stepRadians);

    // to moved onto the line through from parallel or square to direction, when the line from
    // from to to is within toleranceRadians of either; nothing otherwise.
    [[nodiscard]] std::optional<SnapResult> SnapToDirection(Vec2 from, Vec2 to, Vec2 direction, float toleranceRadians);
}
