#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Shapes painted on the ground, in metres in the city's ground plane (x and z). Everything here
// is plain geometry: no game calls, so it can be tested on its own.
namespace Ground {
    struct Vec2 {
        float x = 0.0f;
        float z = 0.0f;

        bool operator==(const Vec2&) const = default;
    };

    using Ring = std::vector<Vec2>;
    // An area: everything inside an odd number of rings (the even-odd rule), which is also how
    // the terrain paint service reads a shape. Rings coming out of these functions never overlap.
    using Area = std::vector<Ring>;

    enum class PathStyle {
        Straight,  // straight lines between the points
        Curved,    // a smooth curve through the points
    };

    enum class LineEnds {
        Flat,
        Round,
    };

    // The points a curve takes through the anchors, about every spacing metres. Straight paths
    // come back as the anchors. A closed path goes back to its first anchor.
    [[nodiscard]] std::vector<Vec2> PathThrough(std::span<const Vec2> anchors, PathStyle style, bool closed,
                                                float spacing = 0.5f);

    // The path a pen draws. Each anchor has two handles, offsets from it: the curve leaves along
    // handles and arrives along inHandles. Zero handles make a corner. A missing handle counts as
    // zero, a missing in-handle as the mirror of its handle, and a negative radius as
    // defaultRadius. A corner between two straight sides is rounded with its radius, at most as
    // far as half the shorter side.
    [[nodiscard]] std::vector<Vec2> PenPath(std::span<const Vec2> anchors, std::span<const Vec2> handles,
                                            std::span<const Vec2> inHandles, std::span<const float> radii,
                                            float defaultRadius, bool closed, float spacing = 0.25f);

    // Drops points of a hand-drawn path that stray less than tolerance from the line, so a
    // freehand stroke becomes a few anchors a curve can go through.
    [[nodiscard]] std::vector<Vec2> SimplifyPath(std::span<const Vec2> points, float tolerance, bool closed);

    // The area inside a closed outline. With a corner radius, convex and concave corners are
    // rounded; parts narrower than twice the radius disappear.
    [[nodiscard]] Area AreaFromOutline(std::span<const Vec2> outline, float cornerRadius = 0.0f);

    struct Dashes {
        float dashLength = 0.0f;  // 0: a solid line
        float gapLength = 0.0f;
    };

    // A line of the given width along a path, optionally dashed.
    [[nodiscard]] Area StrokePath(std::span<const Vec2> path, float width, LineEnds ends, Dashes dashes = {});

    // What a round brush of the given radius covers when dragged along the samples.
    [[nodiscard]] Area BrushStroke(std::span<const Vec2> samples, float radius);

    // A corner of a UV map triangle: a ground point and its texture coordinates.
    struct UvVertex {
        float x = 0.0f;
        float z = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
    };

    // Triangles along a path, three corners each, that make a texture follow it: u runs along the
    // path, one texture every repeatLength metres, and v across it, from 0 on the left edge to 1
    // on the right, halfWidth from the path. The strip goes on halfWidth past both ends, so it
    // also covers round line ends.
    [[nodiscard]] std::vector<UvVertex> PathUvStrip(std::span<const Vec2> path, float halfWidth, float repeatLength);

    [[nodiscard]] Area Unite(const Area& a, const Area& b);
    [[nodiscard]] Area Subtract(const Area& from, const Area& cut);
    [[nodiscard]] Area Intersect(const Area& a, const Area& b);
    [[nodiscard]] Area Grow(const Area& area, float distance);

    [[nodiscard]] double AreaOf(const Area& area);
    // Even-odd, without copying the rings: cheap enough to call for every mouse move.
    [[nodiscard]] bool Contains(const Area& area, Vec2 point);
    [[nodiscard]] bool IsEmpty(const Area& area);

    struct Bounds {
        Vec2 min{};
        Vec2 max{};
    };
    [[nodiscard]] Bounds BoundsOf(const Area& area);
    [[nodiscard]] bool Overlaps(const Bounds& a, const Bounds& b);
    [[nodiscard]] bool Contains(const Bounds& bounds, Vec2 point, float margin = 0.0f);
    [[nodiscard]] Area Transformed(const Area& area, Vec2 pivot, float rotationRadians, float scale, Vec2 offset);
    // The area mirrored in the line through pivot at angle (radians) to the x axis.
    [[nodiscard]] Area Mirrored(const Area& area, Vec2 pivot, float axisRadians);
    [[nodiscard]] Vec2 MirroredPoint(Vec2 point, Vec2 pivot, float axisRadians);

    // How long the rings are, all together.
    [[nodiscard]] double PerimeterOf(const Area& area);

    // A band along the area's edge, width wide: outside it, inside it, or half and half.
    enum class BorderSide : uint8_t {
        Outside,
        Inside,
        Centred,
    };
    [[nodiscard]] Area BorderOf(const Area& area, float width, BorderSide side);

    // Patterns that move, turn and scale with what they are drawn on: noise is read in this frame.
    struct PatternFrame {
        Vec2 origin{};
        float turnRadians = 0.0f;
    };

    // Smooth noise, about -1 to 1, with detail about every featureSize metres. The same for the
    // same seed and point.
    [[nodiscard]] float PatternNoise(Vec2 point, const PatternFrame& frame, float featureSize, uint32_t seed);

    // The area with a natural, rough outline: the edge wanders up to about amount metres either way.
    [[nodiscard]] Area Roughened(const Area& area, float amount, const PatternFrame& frame, uint32_t seed);

    // Soft-edged patches of the area that cover about coverage (0 to 1) of it, each about
    // patchSize across. Noise is sampled on a grid of at most maxCells cells, so a big area
    // gets coarser patches rather than a slow cut.
    [[nodiscard]] Area PatchesOf(const Area& area, float patchSize, float coverage, const PatternFrame& frame,
                                 uint32_t seed, size_t maxCells);

    // The point of a segment nearest to p.
    [[nodiscard]] Vec2 NearestOnSegment(Vec2 p, Vec2 a, Vec2 b);
}
