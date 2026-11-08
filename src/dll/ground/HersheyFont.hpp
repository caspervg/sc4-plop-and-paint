#pragma once

// The Hershey simplex Roman font, for text painted on the ground. Each glyph is a string in James
// Hurt's format: every character pair is one point, its coordinates as offsets from 'R'. The first
// pair holds the glyph's left and right edge, " R" lifts the pen. y grows downwards; the baseline is
// at y = 9 and capitals reach up to y = -12.
//
// The Hershey Fonts were originally created by Dr. A. V. Hershey while working at the U. S.
// National Bureau of Standards. The format of the font data in this distribution was originally
// created by James Hurt, Cognition, Inc., 900 Technology Park Drive, Billerica, MA 01821.
namespace Ground::Hershey {
    constexpr int kBaseline = 9;
    constexpr int kCapHeight = 21;

    // The glyph of a printable ASCII character, nullptr for anything else.
    [[nodiscard]] const char* Glyph(char c);
}
