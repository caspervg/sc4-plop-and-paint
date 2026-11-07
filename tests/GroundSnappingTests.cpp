// Checks for snapping to painted ground: run GroundSnappingTests.exe, it prints "ok" or aborts.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <vector>

#include "ground/GroundSnapping.hpp"

using namespace Ground;

namespace {
    void Check(const bool condition, const char* what, const int line) {
        if (!condition) {
            std::fprintf(stderr, "FAILED line %d: %s\n", line, what);
            std::abort();
        }
    }
#define CHECK(x) Check((x), #x, __LINE__)

    bool Near(const Vec2 a, const Vec2 b, const float tolerance = 1.0e-3f) {
        return std::hypot(a.x - b.x, a.z - b.z) <= tolerance;
    }
}

int main() {
    SnapIndex index;
    CHECK(!index.Snap({0, 0}, 1.0f));

    // A square: its corners, the middles of its sides and its edges.
    index.AddArea(AreaFromOutline(std::vector<Vec2>{{0, 0}, {20, 0}, {20, 20}, {0, 20}}));
    CHECK(index.PointCount() == 8 && index.SegmentCount() == 4);
    const auto corner = index.Snap({20.4f, 19.7f}, 1.0f);
    CHECK(corner && corner->kind == SnapKind::Point && Near(corner->point, {20, 20}));
    const auto middle = index.Snap({10.3f, 0.4f}, 1.0f);
    CHECK(middle && middle->kind == SnapKind::Middle && Near(middle->point, {10, 0}));
    const auto edge = index.Snap({4.0f, 19.4f}, 1.0f);
    CHECK(edge && edge->kind == SnapKind::Edge && Near(edge->point, {4, 20}));
    CHECK(Near(edge->edgeFrom, {20, 20}) || Near(edge->edgeTo, {20, 20}));
    CHECK(!index.Snap({10, 10}, 1.0f));
    CHECK(!index.Snap({4.0f, 21.5f}, 1.0f));

    // A long edge is found anywhere along it, far from its ends.
    index.AddSegment({-500, 300}, {500, 300}, false);
    const auto far = index.Snap({123.4f, 300.6f}, 1.0f);
    CHECK(far && far->kind == SnapKind::Edge && Near(far->point, {123.4f, 300}));
    // A lone point wins over an edge it lies on.
    index.AddPoint({50, 300});
    const auto point = index.Snap({50.5f, 300.2f}, 1.0f);
    CHECK(point && point->kind == SnapKind::Point && Near(point->point, {50, 300}));

    // A curve with many points is thinned, and adds no corners.
    std::vector<Vec2> circle;
    for (int i = 0; i < 720; ++i) {
        const float a = static_cast<float>(i) * std::numbers::pi_v<float> / 360.0f;
        circle.push_back({1000 + 30 * std::cos(a), 30 * std::sin(a)});
    }
    SnapIndex curves;
    curves.AddArea({circle});
    CHECK(curves.PointCount() == 0 && curves.SegmentCount() < 720);
    const auto onCircle = curves.Snap({1030.5f, 0.0f}, 1.0f);
    CHECK(onCircle && std::abs(onCircle->point.x - 1030.0f) < 0.1f);

    // 15 degree steps keep the distance along the ray.
    const Vec2 stepped = SnapToAngle({0, 0}, {10, 1}, std::numbers::pi_v<float> / 12.0f);
    CHECK(Near(stepped, {10, 0}));
    const Vec2 diagonal = SnapToAngle({0, 0}, {10, 9}, std::numbers::pi_v<float> / 12.0f);
    CHECK(std::abs(diagonal.x - diagonal.z) < 1e-3f);

    // Parallel and square to an edge, within a few degrees only.
    const Vec2 edgeDirection{1, 1};
    const auto parallel = SnapToDirection({0, 0}, {10, 10.3f}, edgeDirection, 0.05f);
    CHECK(parallel && parallel->kind == SnapKind::Parallel && std::abs(parallel->point.x - parallel->point.z) < 1e-3f);
    const auto square = SnapToDirection({0, 0}, {-10, 10.2f}, edgeDirection, 0.05f);
    CHECK(square && square->kind == SnapKind::Square && std::abs(square->point.x + square->point.z) < 1e-3f);
    CHECK(!SnapToDirection({0, 0}, {10, 5}, edgeDirection, 0.05f));

    std::puts("ok");
    return 0;
}
