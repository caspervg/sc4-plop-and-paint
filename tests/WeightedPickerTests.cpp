// Checks for stepping through family entries: run WeightedPickerTests.exe, it prints "ok" or aborts.
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <vector>

#include "common/WeightedPicker.hpp"

namespace {
    void Check(const bool condition, const char* what, const int line) {
        if (!condition) {
            std::fprintf(stderr, "FAILED line %d: %s\n", line, what);
            std::abort();
        }
    }
#define CHECK(x) Check((x), #x, __LINE__)

    WeightedPicker Picker(const std::initializer_list<uint32_t> ids) {
        std::vector<FamilyEntry> entries;
        for (const uint32_t id : ids) {
            FamilyEntry entry;
            entry.propID = id;
            entries.push_back(entry);
        }
        return WeightedPicker(entries, 1);
    }
}

int main() {
    const WeightedPicker cycle = Picker({10, 20, 30});
    CHECK(cycle.Cycle(10) == 20);
    CHECK(cycle.Cycle(20) == 30);
    CHECK(cycle.Cycle(30) == 10);
    CHECK(cycle.Cycle(99) == 10);

    CHECK(cycle.Cycle(30, true) == 20);
    CHECK(cycle.Cycle(20, true) == 10);
    CHECK(cycle.Cycle(10, true) == 30);
    CHECK(cycle.Cycle(99, true) == 30);

    // A repeated entry must not trap the step, in either direction.
    const WeightedPicker repeated = Picker({10, 10, 20});
    CHECK(repeated.Cycle(10) == 20);
    CHECK(repeated.Cycle(20) == 10);
    CHECK(repeated.Cycle(10, true) == 20);
    CHECK(repeated.Cycle(20, true) == 10);

    CHECK(Picker({10, 10}).Cycle(10) == 10);
    CHECK(Picker({}).Cycle(10) == 10);
    CHECK(Picker({}).Cycle(10, true) == 10);

    std::puts("ok");
    return 0;
}
