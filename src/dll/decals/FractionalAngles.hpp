#pragma once
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

// NAM fractional-angle (FA) network angles.
namespace fa_angles {

struct Preset {
    float degrees;
    std::string_view label;
};

// Quadrant presets for dropdowns: 0-90 deg inclusive.
inline constexpr std::array<Preset, 11> kQuadrantPresets{{
    {0.0f, "0\xC2\xB0 (Orthogonal)"},
    {9.5f, "9.5\xC2\xB0 (FA-6)"},
    {18.4f, "18.4\xC2\xB0 (FA-3)"},
    {26.6f, "26.6\xC2\xB0 (FA-2)"},
    {33.7f, "33.7\xC2\xB0 (FA-1.5)"},
    {45.0f, "45\xC2\xB0 (Diagonal)"},
    {56.3f, "56.3\xC2\xB0 (FA-1.5)"},
    {63.4f, "63.4\xC2\xB0 (FA-2)"},
    {71.6f, "71.6\xC2\xB0 (FA-3)"},
    {80.5f, "80.5\xC2\xB0 (FA-6)"},
    {90.0f, "90\xC2\xB0 (Orthogonal)"},
}};

// Full circle snap points for keyboard cycling, sorted ascending, no dup endpoints.
inline constexpr std::array<float, 40> kFullCircleDegrees{{
    0.0f, 9.5f, 18.4f, 26.6f, 33.7f, 45.0f, 56.3f, 63.4f, 71.6f, 80.5f,
    90.0f, 99.5f, 108.4f, 116.6f, 123.7f, 135.0f, 146.3f, 153.4f, 161.6f, 170.5f,
    180.0f, 189.5f, 198.4f, 206.6f, 213.7f, 225.0f, 236.3f, 243.4f, 251.6f, 260.5f,
    270.0f, 279.5f, 288.4f, 296.6f, 303.7f, 315.0f, 326.3f, 333.4f, 341.6f, 350.5f,
}};

// Label for whichever quadrant preset the given angle (any degree value) is
// closest to; "Custom (xx.xx)" with the raw angle if none match.
inline std::string Label(const float degrees) {
    const float quadrantBase = std::floor(degrees / 90.0f) * 90.0f;
    const float quadrantOffset = degrees - quadrantBase;
    for (const Preset& preset : kQuadrantPresets) {
        if (std::fabs(preset.degrees - quadrantOffset) < 0.05f) {
            return std::string(preset.label);
        }
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "Custom (%.2f)", degrees);
    return buf;
}

}  // namespace fa_angles
