#pragma once

#include <cstdint>
#include <span>
#include <string>

#include "GroundGeometry.hpp"

// Road markings: parking stalls, zebra crossings, arrows, double lines, hatching and text, made
// from a few clicked points. Plain geometry like GroundGeometry, so it can be tested on its own.
//
// "Left" is left of the direction of travel as seen from above in the game's top view, with x to
// the right and z downwards: for travel along +x, left is -z. Text reads the right way round in
// that view.
namespace Ground {
    enum class MarkingKind : uint8_t {
        ParkingRow,
        CarPark,
        Zebra,
        Arrow,
        DoubleLine,
        Hatching,
        Text,
    };

    enum class ArrowKind : uint8_t {
        Straight,
        Left,
        Right,
        StraightLeft,
        StraightRight,
        LeftRight,
        UTurn,
    };

    struct MarkingSpec {
        MarkingKind kind = MarkingKind::ParkingRow;
        float lineWidth = 0.12f;  // paint stroke width, all kinds

        // ParkingRow and CarPark: the points are the front edge of the (first) row, with the stalls
        // on its left. The stall width is measured square to the dividers.
        float stallWidth = 2.5f;
        float stallDepth = 5.0f;
        float stallAngleDegrees = 90.0f;  // between the dividers and the row, 30 to 90
        bool closeFront = false;          // a line along the stall fronts
        int rowPairs = 1;                 // CarPark: pairs of rows back to back, aisles between
        float aisleWidth = 6.0f;

        // Zebra: two points across the road, kerb to kerb; the stripes run along the road.
        float stripeWidth = 0.5f;
        float stripeGap = 0.5f;
        float stripeLength = 4.0f;

        // DoubleLine: two lines along the path, lineSpacing apart (the gap between them).
        float lineSpacing = 0.15f;
        float dashLength = 0.0f;  // 0: solid
        float gapLength = 0.0f;

        // Hatching: stripes inside a closed outline, at an angle to its first edge.
        float hatchSpacing = 1.0f;
        float hatchAngleDegrees = 45.0f;
        bool hatchOutline = true;
        bool chevron = false;  // stripes mirrored about the first edge's line through the centre

        // Arrow: two points, tail to tip.
        ArrowKind arrow = ArrowKind::Straight;
        float arrowWidth = 1.2f;  // width of the head

        // Text: the baseline starts at the first point and runs towards the second.
        std::string text{};
        float textHeight = 1.5f;     // capital height
        float letterSpacing = 0.1f;  // extra space between letters, as a share of the height

        bool operator==(const MarkingSpec&) const = default;
    };

    struct MarkingPoints {
        size_t minPoints;
        size_t maxPoints;  // 0: any number
        bool closed;
    };

    [[nodiscard]] MarkingPoints PointsFor(MarkingKind kind);

    // The painted area of a marking. Empty with fewer points than the kind needs.
    [[nodiscard]] Area BuildMarking(const MarkingSpec& spec, std::span<const Vec2> points, PathStyle pathStyle);

    // How big the patches of worn paint should be, across, for paint of this shape: about its
    // thickness, so lines wear through in places and wide areas get scuffed. SC4RenderServices
    // cuts them when it draws the paint.
    [[nodiscard]] float WearPatchSize(const Area& area);

    [[nodiscard]] const char* MarkingKindName(MarkingKind kind);
}
