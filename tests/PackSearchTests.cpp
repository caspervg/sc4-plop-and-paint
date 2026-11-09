// Checks for finding packs and files by name: run PackSearchTests.exe, it prints "ok" or aborts.
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
    constexpr const char* kFile = "BSC MEGA Props - ALN Vol01 v2";

    CHECK(ContainsAllWords(kFile, "BSC MEGA Props ALN Vol"));
    CHECK(ContainsAllWords(kFile, "bsc  mega props - aln"));
    CHECK(ContainsAllWords(kFile, "vol01 aln"));
    CHECK(ContainsAllWords(kFile, ""));
    CHECK(ContainsAllWords(kFile, "   "));
    CHECK(!ContainsAllWords(kFile, "BSC MEGA Props SG"));
    CHECK(!ContainsAllWords("", "bsc"));

    std::puts("ok");
    return 0;
}
