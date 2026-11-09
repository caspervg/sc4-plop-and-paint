// Checks for the search boxes: run TextSearchTests.exe, it prints "ok" or aborts.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "common/TextSearch.hpp"

namespace {
    void Check(const bool condition, const char* what, const int line) {
        if (!condition) {
            std::fprintf(stderr, "FAILED line %d: %s\n", line, what);
            std::abort();
        }
    }
#define CHECK(x) Check((x), #x, __LINE__)

    bool Finds(const char* typed, const char* name) {
        return TextSearch::Query(typed).Matches(TextSearch::Normalize(name));
    }
}

int main() {
    using TextSearch::Normalize;
    using TextSearch::Query;
    using TextSearch::Suggester;

    CHECK(Normalize("BSC-Vol01") == " bsc vol01 vol 1 ");
    CHECK(Normalize("") == " ");

    // Any order, any case, punctuation does not count.
    constexpr const char* kFile = "BSC MEGA Props - ALN Vol01";
    CHECK(Finds("BSC MEGA Props ALN Vol", kFile));
    CHECK(Finds("bsc  mega props - aln", kFile));
    CHECK(Finds("aln bsc", kFile));
    CHECK(Finds("meg", kFile));
    CHECK(!Finds("BSC MEGA Props SG", kFile));

    // An empty search finds everything.
    CHECK(Query("").Empty());
    CHECK(Query(" - ").Empty());
    CHECK(Finds("", kFile));
    CHECK(Finds("   ", kFile));

    // Numbers.
    CHECK(Finds("vol 1", kFile));
    CHECK(Finds("vol01", kFile));
    CHECK(Finds("vol1", kFile));
    CHECK(Finds("vol 01", kFile));
    CHECK(!Finds("vol 10", kFile));
    CHECK(!Finds("vol 2", kFile));
    CHECK(!Finds("vol 1", "Prop Vol12"));
    CHECK(Finds("1x1", "Fence 1x1 corner"));

    // IDs are found from the middle, in any case.
    const std::string id = Normalize(TextSearch::HexId(0x25A70004));
    CHECK(Query("25a7").Matches(id));
    CHECK(Query("25A70004").Matches(id));
    CHECK(Query("0x25a7").Matches(id));
    CHECK(Query("0004").Matches(id));
    CHECK(!Query("25a8").Matches(id));

    // Leaving out.
    CHECK(Finds("tree -winter", "Oak tree summer"));
    CHECK(!Finds("tree -winter", "Oak tree winter"));
    CHECK(Finds("-winter", "Oak tree"));
    CHECK(!Finds("oak -tree-winter", "Oak winter"));

    // Did you mean.
    Suggester suggester;
    suggester.Add(Normalize("Oak tree medium"));
    suggester.Add(Normalize("Oak tree large"));
    suggester.Add(Normalize("Ask pole"));
    CHECK(suggester.Suggest("oka tree") == "oak tree");
    CHECK(suggester.Suggest("Medum") == "medium");
    CHECK(suggester.Suggest("tree") == std::nullopt);
    CHECK(suggester.Suggest("tre") == std::nullopt);       // part of a known word
    CHECK(suggester.Suggest("zzzzzz") == std::nullopt);    // nothing close
    CHECK(suggester.Suggest("ok") == std::nullopt);        // too short to correct
    CHECK(suggester.Suggest("-oka") == std::nullopt);      // left out on purpose
    CHECK(suggester.Suggest("oak-oka") == "oak-oak");
    CHECK(suggester.Suggest("vol1x") == std::nullopt);     // words with digits stay
    CHECK(suggester.Suggest("") == std::nullopt);
    CHECK(suggester.Suggest("aks") == "ask");              // a swap of two letters is one edit
    CHECK(suggester.Suggest("osk") == "oak");              // the most common wins a tie

    // A new list starts over.
    suggester.Clear();
    CHECK(suggester.Suggest("oka") == std::nullopt);

    std::puts("ok");
    return 0;
}
