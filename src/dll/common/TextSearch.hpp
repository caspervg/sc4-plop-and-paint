#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// The search that every search box uses. Words count in any order and any case, punctuation does
// not count, "vol 1" finds "Vol01", and "-winter" leaves out what has that word.
//
// Lists keep one Normalize() string per item, made once, so that typing only scans those strings.
namespace TextSearch {
    // How a name looks to the search: between spaces, in lower case, with punctuation as spaces,
    // and with words that have digits also in parts: "Vol01" gives "vol01 vol 1".
    [[nodiscard]] std::string Normalize(std::string_view text);

    // An ID as it goes into the text that Normalize gets, with a space before it.
    [[nodiscard]] std::string HexId(uint32_t id);

    class Query {
    public:
        Query() = default;
        explicit Query(std::string_view typed);

        // Nothing to search for, for example only punctuation. An empty query matches everything.
        [[nodiscard]] bool Empty() const { return include_.empty() && exclude_.empty(); }
        // normalized comes from Normalize.
        [[nodiscard]] bool Matches(std::string_view normalized) const;

    private:
        struct Term {
            std::string needle;
            // Words with digits can also match in parts: "vol1" finds "vol 1".
            std::vector<Term> parts;
        };

        [[nodiscard]] static Term MakeTerm(std::string_view word);
        [[nodiscard]] static bool Matches_(const Term& term, std::string_view normalized);

        std::vector<Term> include_;
        std::vector<Term> exclude_;
    };

    // "Did you mean": corrects words of a search that are close to a word in the list. For when
    // a search finds nothing, so a typo cannot add wrong matches to normal results.
    class Suggester {
    public:
        void Clear();
        // normalized comes from Normalize.
        void Add(std::string_view normalized);
        // The search with its unknown words corrected, or nothing when there is no correction.
        [[nodiscard]] std::optional<std::string> Suggest(std::string_view typed);

    private:
        // Nothing when the word is known, or when no word is close.
        [[nodiscard]] std::optional<std::string> Correction_(const std::string& word) const;

        std::unordered_map<std::string, uint32_t> counts_;
        std::vector<std::pair<std::string, uint32_t>> words_;  // most common first
        bool wordsStale_ = false;
        std::optional<std::string> lastTyped_;
        std::optional<std::string> lastResult_;
    };
}
