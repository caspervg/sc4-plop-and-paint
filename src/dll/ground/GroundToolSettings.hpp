#pragma once

#include <cstdint>

#include "GroundPaintModel.hpp"

namespace Ground {
    // A plain white terrain decal texture in SC4PlopAndPaint.dat, tinted for paint in a colour.
    // The instance does not end in 4, 9 or E, so the decal list leaves it out.
    constexpr uint32_t kWhiteTextureInstance = 0xB1A2C301;

    enum class Tool : uint8_t {
        Area,
        Line,
        Brush,
        Marking,
        Erase,
        Select,
    };

    enum class DrawStyle : uint8_t {
        Clicks,    // straight lines between clicked points
        Curve,     // a smooth curve through clicked points
        Freehand,  // drag to draw, smoothed
    };

    struct ToolSettings {
        Tool tool = Tool::Area;
        DrawStyle drawStyle = DrawStyle::Clicks;
        float cornerRadius = 0.0f;
        // SC4 draws a 16 m tile about 128 pixels across at its closest zoom: real-world marking widths
        // (10 to 15 cm) come out at a pixel or less. Its own road markings are drawn about this wide.
        float lineWidth = 0.3f;
        LineEnds lineEnds = LineEnds::Flat;
        float dashLength = 0.0f;
        float gapLength = 0.0f;
        float brushRadius = 4.0f;
        bool eraseWithBrush = true;
        MarkingSpec marking{.lineWidth = 0.3f};
        float wear = 0.0f;
        Look look{};
        // The grid, as the other painters have it.
        bool showGrid = true;
        bool snapPointsToGrid = false;
        float gridStepMeters = 16.0f;
    };
}
