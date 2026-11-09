// Checks for filling a polygon with props or flora: run PolygonPlacerTests.exe, it prints "ok" or aborts.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <utility>
#include <vector>

#include "paint/PaintSettings.hpp"
#include "paint/PolygonPlacer.hpp"

namespace {
    void Check(const bool condition, const char* what, const int line) {
        if (!condition) {
            std::fprintf(stderr, "FAILED line %d: %s\n", line, what);
            std::abort();
        }
    }
#define CHECK(x) Check((x), #x, __LINE__)

    std::vector<cS3DVector3> Outline(const std::vector<std::pair<float, float>>& points) {
        std::vector<cS3DVector3> result;
        for (const auto& [x, z] : points) {
            result.emplace_back(x, 0.0f, z);
        }
        return result;
    }

    std::vector<PlannedPaint> Rows(const std::vector<cS3DVector3>& outline, const RowLayout& layout, const float margin = 0.0f,
                                   const int32_t rotation = 0) {
        return PolygonPlacer::ComputeRowPlacements(outline, layout, margin, rotation, false, nullptr, 1,
                                                   PolygonHeightSamplingMode::Terrain, nullptr, 7);
    }

    bool Near(const float a, const float b) { return std::abs(a - b) < 0.01f; }
}

int main() {
    const auto field = Outline({{0, 0}, {20, 0}, {20, 10}, {0, 10}});

    // Rows along x, centred in the field: 5 rows of 4 items.
    {
        const auto placed = Rows(field, {.spacingMeters = 2.0f, .stepMeters = 5.0f, .angleDegrees = 0.0f});
        CHECK(placed.size() == 20);
        std::set<int> rows;
        for (const PlannedPaint& p : placed) {
            rows.insert(static_cast<int>(std::lround(p.position.fZ * 10.0f)));
            CHECK(p.itemId == 7 && p.rotation == 0);
        }
        CHECK(rows == std::set<int>({10, 30, 50, 70, 90}));
        CHECK(Near(placed.front().position.fX, 2.5f) && Near(placed.front().position.fZ, 1.0f));
    }

    // A row angle of 90 degrees: the rows run along z.
    {
        const auto placed = Rows(field, {.spacingMeters = 5.0f, .stepMeters = 2.0f, .angleDegrees = 90.0f});
        CHECK(placed.size() == 20);
        std::set<int> rows;
        for (const PlannedPaint& p : placed) {
            rows.insert(static_cast<int>(std::lround(p.position.fX * 10.0f)));
        }
        CHECK(rows == std::set<int>({25, 75, 125, 175}));
    }

    // Too small for a full step or row: one item in the middle.
    {
        const auto placed = Rows(Outline({{0, 0}, {2, 0}, {2, 2}, {0, 2}}), {.spacingMeters = 6.0f, .stepMeters = 6.0f});
        CHECK(placed.size() == 1 && Near(placed[0].position.fX, 1.0f) && Near(placed[0].position.fZ, 1.0f));
    }

    // A U shape: nothing in the notch.
    {
        const auto shape = Outline({{0, 0}, {30, 0}, {30, 30}, {20, 30}, {20, 10}, {10, 10}, {10, 30}, {0, 30}});
        const auto placed = Rows(shape, {.spacingMeters = 3.0f, .stepMeters = 3.0f});
        CHECK(!placed.empty());
        for (const PlannedPaint& p : placed) {
            CHECK(!(p.position.fX > 10.0f && p.position.fX < 20.0f && p.position.fZ > 10.0f));
        }
    }

    // The edge margin keeps rows and scatter inside.
    {
        for (const PlannedPaint& p : Rows(field, {.spacingMeters = 1.0f, .stepMeters = 1.0f}, 3.0f)) {
            CHECK(p.position.fX >= 3.0f && p.position.fX <= 17.0f && p.position.fZ >= 3.0f && p.position.fZ <= 7.0f);
        }
        const auto scattered = PolygonPlacer::ComputePlacements(field, 10.0f, 0.0f, 0, false, nullptr, 1,
                                                                PolygonHeightSamplingMode::Terrain, nullptr, 7,
                                                                static_cast<size_t>(-1), 3.0f);
        CHECK(!scattered.empty());
        for (const PlannedPaint& p : scattered) {
            CHECK(p.position.fX >= 3.0f && p.position.fX <= 17.0f && p.position.fZ >= 3.0f && p.position.fZ <= 7.0f);
        }
    }

    // Items turn with the rows, on top of the chosen rotation, as along a line.
    {
        const auto along = Rows(field, {.spacingMeters = 5.0f, .stepMeters = 5.0f, .angleDegrees = 90.0f, .alignToRows = true}, 0.0f, 1);
        CHECK(!along.empty() && along.front().rotation == 1);
        const auto across = Rows(field, {.spacingMeters = 5.0f, .stepMeters = 5.0f, .angleDegrees = 0.0f, .alignToRows = true}, 0.0f, 1);
        CHECK(!across.empty() && across.front().rotation == 2);
    }

    // The limit is respected.
    {
        const auto placed = PolygonPlacer::ComputeRowPlacements(field, {.spacingMeters = 1.0f, .stepMeters = 1.0f}, 0.0f, 0, false,
                                                                nullptr, 1, PolygonHeightSamplingMode::Terrain, nullptr, 7, 12);
        CHECK(placed.size() == 12);
    }

    // The M key: every stop in turn, and back to the start.
    const auto next = [](const PaintMode mode, const bool ground, const bool sketching, const bool painted) {
        const PaintStop stop = NextPaintStop(mode, ground, sketching, painted);
        return std::pair(stop.mode, stop.fromGround);
    };
    using Stop = std::pair<PaintMode, bool>;
    CHECK(next(PaintMode::Direct, false, false, true) == Stop(PaintMode::Line, false));
    CHECK(next(PaintMode::Line, false, false, true) == Stop(PaintMode::Polygon, false));
    CHECK(next(PaintMode::Polygon, false, false, true) == Stop(PaintMode::Polygon, true));
    CHECK(next(PaintMode::Polygon, true, false, true) == Stop(PaintMode::Direct, false));
    // Without painted ground that stop is skipped.
    CHECK(next(PaintMode::Polygon, false, false, false) == Stop(PaintMode::Direct, false));
    // A sketch stays a sketch.
    CHECK(next(PaintMode::Line, false, true, true) == Stop(PaintMode::Polygon, false));
    CHECK(next(PaintMode::Polygon, false, true, true) == Stop(PaintMode::Line, false));

    std::puts("ok");
    return 0;
}
