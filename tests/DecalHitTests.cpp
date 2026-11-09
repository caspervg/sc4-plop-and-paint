// Checks for pointing at decals and naming their angles: run DecalHitTests.exe, it prints "ok" or aborts.
#include <cstdio>
#include <cstdlib>

#include "decals/DecalHit.hpp"
#include "decals/FractionalAngles.hpp"

namespace {
    void Check(const bool condition, const char* what, const int line) {
        if (!condition) {
            std::fprintf(stderr, "FAILED line %d: %s\n", line, what);
            std::abort();
        }
    }
#define CHECK(x) Check((x), #x, __LINE__)

    struct Center {
        float fX = 0.0f;
        float fY = 0.0f;
    };

    struct Info {
        Center center;
        float baseSize = 16.0f;
        float aspectMultiplier = 1.0f;
        float rotationTurns = 0.0f;
    };

    struct State {
        Info decalInfo;
    };

    State Decal(const float size, const float aspect = 1.0f, const float turns = 0.0f) {
        State state;
        state.decalInfo.baseSize = size;
        state.decalInfo.aspectMultiplier = aspect;
        state.decalInfo.rotationTurns = turns;
        return state;
    }
}

int main() {
    using decals::MeasureDecalHit;

    // Near the centre: the nearest wins, whatever the size.
    CHECK(MeasureDecalHit(Decal(4.0f), 1.0f, 0.0f).has_value());
    CHECK(*MeasureDecalHit(Decal(4.0f), 1.0f, 0.0f) < *MeasureDecalHit(Decal(4.0f), 3.0f, 0.0f));
    CHECK(!MeasureDecalHit(Decal(4.0f), 9.0f, 0.0f).has_value());

    // Away from the centre of a large decal: a hit, but after anything near its centre.
    const auto far = MeasureDecalHit(Decal(64.0f), 25.0f, 0.0f);
    CHECK(far.has_value());
    CHECK(*MeasureDecalHit(Decal(4.0f), 7.0f, 0.0f) < *far);
    CHECK(*MeasureDecalHit(Decal(40.0f), 15.0f, 0.0f) < *far);
    CHECK(!MeasureDecalHit(Decal(64.0f), 33.0f, 0.0f).has_value());

    // Long and thin, turned as the preview outline turns it.
    const State turned = Decal(4.0f, 10.0f, 0.125f);
    CHECK(MeasureDecalHit(turned, 12.7f, -12.7f).has_value());
    CHECK(!MeasureDecalHit(turned, 12.7f, 12.7f).has_value());

    // A lot texture stays in its own set of zoom levels.
    CHECK(decals::ClosestZoomTexture(0x12345670u) == 0x12345674u);
    CHECK(decals::ClosestZoomTexture(0x12345674u) == 0x12345674u);
    CHECK(decals::ClosestZoomTexture(0x12345675u) == 0x12345679u);
    CHECK(decals::ClosestZoomTexture(0x12345679u) == 0x12345679u);
    CHECK(decals::ClosestZoomTexture(0x1234567Au) == 0x1234567Eu);
    CHECK(decals::ClosestZoomTexture(0x1234567Eu) == 0x1234567Eu);
    CHECK(decals::ClosestZoomTexture(0x1234567Fu) == 0x1234567Fu);

    // The angle shown is the real one, in every quarter of the circle.
    CHECK(fa_angles::Label(0.0f) == "0\xC2\xB0 (Orthogonal)");
    CHECK(fa_angles::Label(90.0f) == "90\xC2\xB0 (Orthogonal)");
    CHECK(fa_angles::Label(99.5f) == "99.5\xC2\xB0 (FA-6)");
    CHECK(fa_angles::Label(350.5f) == "350.5\xC2\xB0 (FA-6)");
    CHECK(fa_angles::Label(359.98f) == "0\xC2\xB0 (Orthogonal)");
    CHECK(fa_angles::Label(12.0f) == "Custom (12.00)");

    std::puts("ok");
    return 0;
}
