// Checks for the road markings: run GroundMarkingsTests.exe, it prints "ok" or aborts.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "ground/GroundMarkings.hpp"

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

    MarkingSpec Spec(const MarkingKind kind) {
        MarkingSpec spec;
        spec.kind = kind;
        return spec;
    }

    const std::vector<Vec2> kRow{{0, 0}, {25, 0}};
}

int main() {
    {
        // A straight 25 m row of 2.5 m stalls: 11 dividers, 5 m deep, on the left (-z for +x).
        const Area row = BuildMarking(Spec(MarkingKind::ParkingRow), kRow, PathStyle::Straight);
        CHECK(row.size() == 11);
        CHECK(Near(AreaOf(row), 11 * 5 * 0.12, 0.05));
        const Bounds b = BoundsOf(row);
        CHECK(Near(b.min.z, -5.0, 0.01) && Near(b.max.z, 0.0, 0.01));

        // Angled stalls are spaced further apart along the row, and a front line joins them.
        MarkingSpec angled = Spec(MarkingKind::ParkingRow);
        angled.stallAngleDegrees = 60.0f;
        angled.closeFront = true;
        const Area joined = BuildMarking(angled, kRow, PathStyle::Straight);
        CHECK(joined.size() == 1);
        CHECK(Near(BoundsOf(joined).min.z, -5.0, 0.07));

        // Curved rows work too.
        const std::vector<Vec2> bend{{0, 0}, {10, 2}, {20, 0}};
        CHECK(!IsEmpty(BuildMarking(Spec(MarkingKind::ParkingRow), bend, PathStyle::Curved)));
    }
    {
        // A car park: two rows back to back, joined by the line between them.
        const Area park = BuildMarking(Spec(MarkingKind::CarPark), kRow, PathStyle::Straight);
        CHECK(park.size() == 1);
        const Bounds b = BoundsOf(park);
        CHECK(Near(b.min.z, -10.0, 0.01) && Near(b.max.z, 0.0, 0.07));

        MarkingSpec two = Spec(MarkingKind::CarPark);
        two.rowPairs = 2;
        two.aisleWidth = 6.0f;
        CHECK(Near(BoundsOf(BuildMarking(two, kRow, PathStyle::Straight)).min.z, -26.0, 0.01));
    }
    {
        // Ten 0.5 m stripes with 0.5 m gaps fit across a 10 m road.
        const std::vector<Vec2> across{{0, 0}, {10, 0}};
        const Area zebra = BuildMarking(Spec(MarkingKind::Zebra), across, PathStyle::Straight);
        CHECK(zebra.size() == 10);
        CHECK(Near(AreaOf(zebra), 10 * 0.5 * 4.0, 0.01));
    }
    {
        // Two lines, each 0.12 m wide.
        const std::vector<Vec2> path{{0, 0}, {20, 0}};
        const Area lines = BuildMarking(Spec(MarkingKind::DoubleLine), path, PathStyle::Straight);
        CHECK(lines.size() == 2);
        CHECK(Near(AreaOf(lines), 2 * 20 * 0.12, 0.01));
    }
    {
        // Arrows stay between tail and tip; a left turn goes to the left.
        const std::vector<Vec2> arrow{{0, 0}, {5, 0}};
        const Area straight = BuildMarking(Spec(MarkingKind::Arrow), arrow, PathStyle::Straight);
        CHECK(!IsEmpty(straight));
        const Bounds b = BoundsOf(straight);
        CHECK(b.min.x >= -0.01 && b.max.x <= 5.01 && b.min.z >= -0.61 && b.max.z <= 0.61);

        MarkingSpec left = Spec(MarkingKind::Arrow);
        left.arrow = ArrowKind::Left;
        const Bounds l = BoundsOf(BuildMarking(left, arrow, PathStyle::Straight));
        CHECK(l.min.z < -1.0 && l.max.z < 0.5);

        for (const ArrowKind kind : {ArrowKind::Right, ArrowKind::StraightLeft, ArrowKind::StraightRight,
                                     ArrowKind::LeftRight, ArrowKind::UTurn}) {
            MarkingSpec spec = Spec(MarkingKind::Arrow);
            spec.arrow = kind;
            CHECK(!IsEmpty(BuildMarking(spec, arrow, PathStyle::Straight)));
        }
    }
    {
        // Hatching stays inside its outline (plus half a line for the outline itself).
        const std::vector<Vec2> square{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
        const Area hatch = BuildMarking(Spec(MarkingKind::Hatching), square, PathStyle::Straight);
        const Bounds b = BoundsOf(hatch);
        CHECK(b.min.x >= -0.07 && b.max.x <= 10.07 && b.min.z >= -0.07 && b.max.z <= 10.07);
        CHECK(AreaOf(hatch) > 12.0 && AreaOf(hatch) < 25.0);

        MarkingSpec chevron = Spec(MarkingKind::Hatching);
        chevron.chevron = true;
        chevron.hatchOutline = false;
        const double area = AreaOf(BuildMarking(chevron, square, PathStyle::Straight));
        CHECK(area > 9.0 && area < 16.0);
    }
    {
        // "AB" reads along +x, capitals 1.5 m tall and above the baseline (-z).
        MarkingSpec spec = Spec(MarkingKind::Text);
        spec.text = "AB";
        const std::vector<Vec2> baseline{{0, 0}, {1, 0}};
        const Area text = BuildMarking(spec, baseline, PathStyle::Straight);
        CHECK(!IsEmpty(text));
        const Bounds b = BoundsOf(text);
        const double scale = 1.5 / 21.0;
        const double width = b.max.x - b.min.x;
        CHECK(width > 0.6 * (18 + 21) * scale && width < 1.1 * (18 + 21) * scale);
        CHECK(Near(b.min.z, -1.5, 0.1) && Near(b.max.z, 0.0, 0.1));

        // Longer text is wider; characters outside the font take a space.
        spec.text = "ABAB";
        CHECK(BoundsOf(BuildMarking(spec, baseline, PathStyle::Straight)).max.x > b.max.x * 1.8);
        spec.text = "A\x01" "B";
        CHECK(!IsEmpty(BuildMarking(spec, baseline, PathStyle::Straight)));
    }
    {
        // Too few or degenerate points give nothing.
        const std::vector<Vec2> one{{3, 3}};
        const std::vector<Vec2> same{{3, 3}, {3, 3}};
        for (const MarkingKind kind : {MarkingKind::ParkingRow, MarkingKind::CarPark, MarkingKind::Zebra,
                                       MarkingKind::Arrow, MarkingKind::DoubleLine, MarkingKind::Hatching,
                                       MarkingKind::Text}) {
            CHECK(BuildMarking(Spec(kind), one, PathStyle::Straight).empty());
            CHECK(IsEmpty(BuildMarking(Spec(kind), same, PathStyle::Straight)));
        }
        CHECK(PointsFor(MarkingKind::Hatching).closed && PointsFor(MarkingKind::Hatching).minPoints == 3);
        CHECK(PointsFor(MarkingKind::Arrow).maxPoints == 2);
    }
    {
        // Worn patches about as big as the paint is thick, within limits.
        const float lineSize = WearPatchSize(StrokePath(kRow, 0.12f, LineEnds::Flat));
        CHECK(lineSize > 0.08f && lineSize < 0.2f);
        const Area square = AreaFromOutline(std::vector<Vec2>{{0, 0}, {10, 0}, {10, 10}, {0, 10}});
        CHECK(WearPatchSize(square) == 1.2f);
        CHECK(WearPatchSize({}) > 0.0f);
    }

    std::puts("ok");
    return 0;
}
