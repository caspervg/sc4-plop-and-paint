// Checks for the ground paint geometry: run GroundGeometryTests.exe, it prints "ok" or aborts.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <vector>

#include "ground/GroundGeometry.hpp"

using namespace Ground;

namespace {
    void Check(const bool condition, const char* what, const int line) {
        if (!condition) {
            std::fprintf(stderr, "FAILED line %d: %s\n", line, what);
            std::abort();
        }
    }
#define CHECK(x) Check((x), #x, __LINE__)

    bool Near(const double a, const double b, const double tolerance) { return std::abs(a - b) <= tolerance; }

    std::vector<Vec2> Square(const float x0, const float z0, const float size) {
        return {{x0, z0}, {x0 + size, z0}, {x0 + size, z0 + size}, {x0, z0 + size}};
    }
}

int main() {
    {
        // Straight paths come back as given; curves pass through every anchor.
        const std::vector<Vec2> anchors{{0, 0}, {10, 5}, {20, 0}, {30, 5}};
        CHECK(PathThrough(anchors, PathStyle::Straight, false) == anchors);
        const auto curve = PathThrough(anchors, PathStyle::Curved, false, 0.5f);
        CHECK(curve.size() > 40);
        for (const Vec2& a : anchors) {
            double nearest = 1e9;
            for (size_t i = 1; i < curve.size(); ++i) {
                const Vec2 p = curve[i - 1];
                const Vec2 q = curve[i];
                const double dx = q.x - p.x;
                const double dz = q.z - p.z;
                const double t = std::clamp(((a.x - p.x) * dx + (a.z - p.z) * dz) / (dx * dx + dz * dz), 0.0, 1.0);
                nearest = std::min(nearest, std::hypot(p.x + t * dx - a.x, p.z + t * dz - a.z));
            }
            CHECK(nearest < 0.05);
        }
        CHECK(Near(curve.front().x, 0, 1e-3) && Near(curve.back().x, 30, 1e-3));
    }
    {
        // A closed curve through a square's corners is round-ish: between the inscribed and the
        // circumscribed circle, and it does not repeat its first point.
        const auto closed = PathThrough(Square(0, 0, 10), PathStyle::Curved, true, 0.25f);
        CHECK(!(closed.front() == closed.back()));
        const double area = AreaOf(AreaFromOutline(closed));
        CHECK(area > std::numbers::pi * 25.0 && area < std::numbers::pi * 50.0);
    }
    {
        // Simplifying a dense straight line leaves its ends.
        std::vector<Vec2> dense;
        for (int i = 0; i <= 100; ++i) {
            dense.push_back({i * 0.1f, (i % 2) * 0.01f});
        }
        const auto simple = SimplifyPath(dense, 0.05f, false);
        CHECK(simple.size() == 2);
    }
    {
        // Outlines: area, holes by even-odd, rounded corners take a little off.
        CHECK(Near(AreaOf(AreaFromOutline(Square(0, 0, 10))), 100.0, 1e-3));
        const Area rounded = AreaFromOutline(Square(0, 0, 10), 2.0f);
        const double cornerLoss = 4.0 * (4.0 - std::numbers::pi);  // four r^2 (1 - pi/4)
        // Arcs are chords inside the circle, so a little less.
        CHECK(Near(AreaOf(rounded), 100.0 - cornerLoss, 0.25));
        CHECK(Contains(rounded, {5, 5}) && !Contains(rounded, {0.1f, 0.1f}));

        const Area outer = AreaFromOutline(Square(0, 0, 10));
        const Area withHole = Subtract(outer, AreaFromOutline(Square(3, 3, 4)));
        CHECK(Near(AreaOf(withHole), 84.0, 1e-3));
        CHECK(!Contains(withHole, {5, 5}) && Contains(withHole, {1, 1}));
        CHECK(Near(AreaOf(Unite(outer, AreaFromOutline(Square(5, 0, 10)))), 150.0, 1e-3));
        CHECK(IsEmpty(AreaFromOutline(std::vector<Vec2>{{0, 0}, {1, 0}, {2, 0}})));
    }
    {
        // Lines: width times length, round ends add a circle, dashes keep about their share.
        const std::vector<Vec2> line{{0, 0}, {20, 0}};
        CHECK(Near(AreaOf(StrokePath(line, 0.5f, LineEnds::Flat)), 10.0, 1e-2));
        CHECK(Near(AreaOf(StrokePath(line, 2.0f, LineEnds::Round)), 40.0 + std::numbers::pi, 0.15));
        const Area dashed = StrokePath(line, 1.0f, LineEnds::Flat, {.dashLength = 3.0f, .gapLength = 2.0f});
        CHECK(Near(AreaOf(dashed), 12.0, 0.05));  // dashes at 0-3, 5-8, 10-13, 15-18
        CHECK(Contains(dashed, {1, 0}) && !Contains(dashed, {4, 0}) && Contains(dashed, {16, 0}));
        CHECK(StrokePath(std::vector<Vec2>{{0, 0}}, 1.0f, LineEnds::Flat).empty());
    }
    {
        // Brush: a click is a circle, a drag a capsule.
        CHECK(Near(AreaOf(BrushStroke(std::vector<Vec2>{{5, 5}}, 2.0f)), std::numbers::pi * 4.0, 0.3));
        CHECK(Near(AreaOf(BrushStroke(std::vector<Vec2>{{0, 0}, {10, 0}}, 1.0f)), 20.0 + std::numbers::pi, 0.15));
    }
    {
        // Grow, bounds and transforms.
        const Area square = AreaFromOutline(Square(0, 0, 10));
        CHECK(Near(AreaOf(Grow(square, 1.0f)), 100.0 + 40.0 + std::numbers::pi, 0.15));
        const Bounds b = BoundsOf(square);
        CHECK(b.min == (Vec2{0, 0}) && b.max == (Vec2{10, 10}));
        const Area turned = Transformed(square, {5, 5}, std::numbers::pi_v<float> / 4.0f, 2.0f, {100, 0});
        CHECK(Near(AreaOf(turned), 400.0, 0.05));
        CHECK(Contains(turned, {105, 5}));
    }

    {
        // A texture strip along a bent path: u is the distance along it in textures, v runs 0 to 1
        // across it, and it covers the stroke of the same width.
        const std::vector<Vec2> path{{0, 0}, {20, 0}, {20, 20}};
        const std::vector<UvVertex> strip = PathUvStrip(path, 2.0f, 10.0f);
        CHECK(strip.size() % 3 == 0 && strip.size() >= 12);
        double covered = 0.0;
        for (size_t i = 0; i < strip.size(); i += 3) {
            const UvVertex& a = strip[i];
            const UvVertex& b = strip[i + 1];
            const UvVertex& c = strip[i + 2];
            covered += std::abs((b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x)) * 0.5;
            for (const UvVertex* v : {&a, &b, &c}) {
                CHECK(v->v == 0.0f || v->v == 1.0f);
                CHECK(v->u >= -0.21f && v->u <= 4.21f);
            }
        }
        const Area stroke = StrokePath(path, 4.0f, LineEnds::Round);
        CHECK(covered >= AreaOf(stroke) - 0.5);
        CHECK(PathUvStrip(std::vector<Vec2>{{1, 1}}, 2.0f, 10.0f).empty());
        CHECK(PathUvStrip(path, 0.0f, 10.0f).empty());
    }

    {
        // Contains: inside, outside, and in a hole.
        Area ring = AreaFromOutline(Square(0, 0, 10));
        const Area hole = AreaFromOutline(Square(4, 4, 2));
        const Area holed = Subtract(ring, hole);
        CHECK(Contains(holed, {1, 1}) && !Contains(holed, {5, 5}) && !Contains(holed, {11, 5}));
        CHECK(Contains(BoundsOf(ring), {10.5f, 5}, 1.0f) && !Contains(BoundsOf(ring), {10.5f, 5}));
    }
    {
        // Borders: outside a square is the bigger square less the square, inside the opposite.
        const Area square = AreaFromOutline(Square(0, 0, 10));
        CHECK(Near(AreaOf(BorderOf(square, 1.0f, BorderSide::Outside)), 144.0 - 100.0, 0.01));
        CHECK(Near(AreaOf(BorderOf(square, 1.0f, BorderSide::Inside)), 100.0 - 64.0, 0.01));
        CHECK(Near(AreaOf(BorderOf(square, 1.0f, BorderSide::Centred)), 121.0 - 81.0, 0.01));
        CHECK(BorderOf(square, 0.0f, BorderSide::Outside).empty());
    }
    {
        // Rough edges wander but keep about the area; the same seed gives the same edge.
        const Area square = AreaFromOutline(Square(0, 0, 20));
        const Area rough = Roughened(square, 0.8f, {}, 42);
        CHECK(Near(AreaOf(rough), 400.0, 40.0));
        CHECK(rough == Roughened(square, 0.8f, {}, 42));
        CHECK(!(rough == Roughened(square, 0.8f, {}, 43)));
        CHECK(Roughened(square, 0.0f, {}, 42) == square);
        // Moving the frame with the shape moves the edge with it.
        const Area moved = Roughened(Transformed(square, {}, 0.0f, 1.0f, {100, 0}), 0.8f, {{100, 0}, 0.0f}, 42);
        CHECK(Near(AreaOf(moved), AreaOf(rough), 0.05));
    }
    {
        // Patches cover about the asked share, inside the area, and fewer cells still work.
        const Area field = AreaFromOutline(Square(0, 0, 100));
        for (const float coverage : {0.2f, 0.5f, 0.8f}) {
            const Area patches = PatchesOf(field, 10.0f, coverage, {}, 9, 60000);
            CHECK(Near(AreaOf(patches) / 10000.0, coverage, 0.08));
            CHECK(Near(AreaOf(Intersect(patches, field)), AreaOf(patches), 0.5));
        }
        CHECK(Near(AreaOf(PatchesOf(field, 10.0f, 0.5f, {}, 9, 500)) / 10000.0, 0.5, 0.1));
        CHECK(PatchesOf(field, 10.0f, 0.0f, {}, 9, 1000).empty());
        CHECK(PatchesOf(field, 10.0f, 1.0f, {}, 9, 1000) == field);
    }
    {
        // Mirrors: a point across the x axis and across a diagonal; areas keep their size.
        const Vec2 p = MirroredPoint({3, 4}, {0, 0}, 0.0f);
        CHECK(Near(p.x, 3, 1e-5) && Near(p.z, -4, 1e-5));
        const Vec2 q = MirroredPoint({3, 4}, {0, 0}, std::numbers::pi_v<float> / 4.0f);
        CHECK(Near(q.x, 4, 1e-5) && Near(q.z, 3, 1e-5));
        const Area l = AreaFromOutline(std::vector<Vec2>{{0, 0}, {4, 0}, {4, 1}, {1, 1}, {1, 3}, {0, 3}});
        const Area mirrored = Mirrored(l, {10, 0}, 0.3f);
        CHECK(Near(AreaOf(mirrored), AreaOf(l), 0.01));
        CHECK(Near(AreaOf(Unite(mirrored, mirrored)), AreaOf(l), 0.01));
    }
    std::puts("ok");
    return 0;
}
