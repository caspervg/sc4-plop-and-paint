// Checks for pointing at props: run PropHitTests.exe, it prints "ok" or aborts.
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "props/PropHit.hpp"

namespace {
    void Check(const bool condition, const char* what, const int line) {
        if (!condition) {
            std::fprintf(stderr, "FAILED line %d: %s\n", line, what);
            std::abort();
        }
    }
#define CHECK(x) Check((x), #x, __LINE__)

    struct Vector {
        float fX = 0.0f;
        float fY = 0.0f;
        float fZ = 0.0f;
    };

    struct Occupant {
        Vector low;
        Vector high;

        Vector* GetBoundingBox(Vector* min, Vector* max) {
            *min = low;
            *max = high;
            return min;
        }
    };

    Occupant Box(const float x1, const float z1, const float x2, const float z2) {
        return {{x1, 0.0f, z1}, {x2, 10.0f, z2}};
    }
}

int main() {
    Occupant large = Box(-15.0f, -15.0f, 15.0f, 15.0f);
    const Vector origin{};

    // Far from the origin, but on the prop.
    CHECK(MeasurePropHit(large, origin, Vector{12.0f, 0.0f, -9.0f}).distance == 0.0f);

    // Off the prop, the distance is to the origin.
    CHECK(std::abs(MeasurePropHit(large, origin, Vector{20.0f, 0.0f, 0.0f}).distance - 20.0f) < 0.001f);

    // A prop with an origin outside its own footprint is still found on the footprint.
    Occupant offset = Box(10.0f, 10.0f, 14.0f, 14.0f);
    CHECK(MeasurePropHit(offset, origin, Vector{12.0f, 0.0f, 12.0f}).distance == 0.0f);

    // A small prop on top of a large one wins.
    Occupant small = Box(-1.0f, -1.0f, 1.0f, 1.0f);
    const Vector cursor{0.5f, 0.0f, 0.5f};
    CHECK(MeasurePropHit(small, origin, cursor) < MeasurePropHit(large, origin, cursor));

    // A direct hit beats a closer origin next to the cursor.
    const Vector smallOrigin{16.0f, 0.0f, 0.0f};
    const Vector edge{14.5f, 0.0f, 0.0f};
    Occupant beside = Box(15.5f, -0.5f, 16.5f, 0.5f);
    CHECK(MeasurePropHit(large, origin, edge) < MeasurePropHit(beside, smallOrigin, edge));

    // A prop without bounds falls back to its origin.
    Occupant empty = Box(0.0f, 0.0f, 0.0f, 0.0f);
    const Vector position{100.0f, 0.0f, 100.0f};
    CHECK(std::abs(MeasurePropHit(empty, position, Vector{102.0f, 0.0f, 100.0f}).distance - 2.0f) < 0.001f);

    std::puts("ok");
    return 0;
}
