// Checks for grouping pack rows: run PackSearchTests.exe, it prints "ok" or aborts.
#include <cstdio>
#include <cstdlib>

#include "packs/PackSearch.hpp"

namespace {
    void Check(const bool condition, const char* what, const int line) {
        if (!condition) {
            std::fprintf(stderr, "FAILED line %d: %s\n", line, what);
            std::abort();
        }
    }
#define CHECK(x) Check((x), #x, __LINE__)
}

int main() {
    // Two members in the same file and pack count once. Another file of that pack stays.
    const auto merged = MergePackRows({{1, 10, 5, 2}, {1, 11, 5, 2}, {1, 12, 6, 2}, {9, 9, 7, 3}});
    CHECK(merged.size() == 3);
    CHECK(merged[0][2] == 5 && merged[0][3] == 2);
    CHECK(merged[1][2] == 6 && merged[1][3] == 2);
    CHECK(merged[2][2] == 7 && merged[2][3] == 3);
    CHECK(MergePackRows({}).empty());

    std::puts("ok");
    return 0;
}
