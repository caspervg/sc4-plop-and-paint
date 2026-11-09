#include "TextSearch.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>

namespace TextSearch {
    namespace {
        // Numbers this short only match as a whole: "vol 1" is not "vol 12". Longer ones are
        // usually parts of IDs.
        constexpr size_t kShortNumber = 3;
        constexpr size_t kMinWordToCorrect = 3;

        // Bytes above ASCII are letters of names in other scripts.
        bool IsWordChar(const char c) {
            const auto byte = static_cast<unsigned char>(c);
            return std::isalnum(byte) != 0 || byte >= 0x80;
        }

        bool IsDigit(const char c) {
            return c >= '0' && c <= '9';
        }

        char Lower(const char c) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        bool IsSpace(const char c) {
            return std::isspace(static_cast<unsigned char>(c)) != 0;
        }

        std::string_view StripZeros(const std::string_view number) {
            const size_t first = number.find_first_not_of('0');
            return first == std::string_view::npos ? number.substr(number.size() - 1) : number.substr(first);
        }

        // Calls back with each run of digits and each run of other characters.
        template <typename Callback>
        void ForEachPart(const std::string_view word, Callback&& callback) {
            size_t start = 0;
            for (size_t i = 1; i <= word.size(); ++i) {
                if (i == word.size() || IsDigit(word[i]) != IsDigit(word[start])) {
                    callback(word.substr(start, i - start));
                    start = i;
                }
            }
        }

        // Calls back with each word of the text, in lower case.
        template <typename Callback>
        void ForEachWord(const std::string_view text, Callback&& callback) {
            std::string word;
            size_t i = 0;
            while (i < text.size()) {
                if (!IsWordChar(text[i])) {
                    ++i;
                    continue;
                }
                word.clear();
                while (i < text.size() && IsWordChar(text[i])) {
                    word += Lower(text[i++]);
                }
                callback(std::string_view(word));
            }
        }

        // Optimal string alignment distance: a swap of two letters next to each other costs one.
        int EditDistance(const std::string_view a, const std::string_view b) {
            std::vector<int> before(b.size() + 1), previous(b.size() + 1), current(b.size() + 1);
            for (size_t j = 0; j <= b.size(); ++j) {
                previous[j] = static_cast<int>(j);
            }
            for (size_t i = 1; i <= a.size(); ++i) {
                current[0] = static_cast<int>(i);
                for (size_t j = 1; j <= b.size(); ++j) {
                    const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
                    current[j] = std::min({previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost});
                    if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) {
                        current[j] = std::min(current[j], before[j - 2] + 1);
                    }
                }
                before.swap(previous);
                previous.swap(current);
            }
            return previous[b.size()];
        }
    }

    std::string Normalize(const std::string_view text) {
        std::string out;
        out.reserve(text.size() * 2 + 2);
        out += ' ';
        ForEachWord(text, [&](const std::string_view word) {
            out += word;
            out += ' ';
            ForEachPart(word, [&](const std::string_view rawPart) {
                const std::string_view part = IsDigit(rawPart.front()) ? StripZeros(rawPart) : rawPart;
                if (part != word) {
                    out += part;
                    out += ' ';
                }
            });
        });
        return out;
    }

    std::string HexId(const uint32_t id) {
        std::array<char, 16> text{};
        std::snprintf(text.data(), text.size(), " 0x%08X", id);
        return text.data();
    }

    Query::Term Query::MakeTerm(const std::string_view word) {
        Term term;
        const bool anyDigit = std::ranges::any_of(word, IsDigit);
        if (std::ranges::all_of(word, IsDigit)) {
            if (word.size() > kShortNumber) {
                term.needle = word;
            }
            else {
                term.needle = ' ';
                term.needle += StripZeros(word);
                term.needle += ' ';
            }
            return term;
        }

        term.needle = word;
        if (anyDigit) {
            ForEachPart(word, [&](const std::string_view part) { term.parts.push_back(MakeTerm(part)); });
        }
        return term;
    }

    Query::Query(const std::string_view typed) {
        size_t i = 0;
        while (i < typed.size()) {
            if (IsSpace(typed[i])) {
                ++i;
                continue;
            }
            const size_t start = i;
            while (i < typed.size() && !IsSpace(typed[i])) {
                ++i;
            }
            std::string_view chunk = typed.substr(start, i - start);

            // A dash alone is punctuation, as in "BSC MEGA Props - ALN".
            const bool excluded = chunk.size() > 1 && chunk[0] == '-' && IsWordChar(chunk[1]);
            if (excluded) {
                chunk.remove_prefix(1);
            }
            ForEachWord(chunk, [&](const std::string_view word) {
                (excluded ? exclude_ : include_).push_back(MakeTerm(word));
            });
        }
    }

    bool Query::Matches_(const Term& term, const std::string_view normalized) {
        if (normalized.find(term.needle) != std::string_view::npos) {
            return true;
        }
        return !term.parts.empty() && std::ranges::all_of(term.parts, [&](const Term& part) {
            return Matches_(part, normalized);
        });
    }

    bool Query::Matches(const std::string_view normalized) const {
        const auto found = [&](const Term& term) { return Matches_(term, normalized); };
        return std::ranges::all_of(include_, found) && std::ranges::none_of(exclude_, found);
    }

    void Suggester::Clear() {
        counts_.clear();
        words_.clear();
        wordsStale_ = false;
        lastTyped_.reset();
        lastResult_.reset();
    }

    void Suggester::Add(const std::string_view normalized) {
        size_t i = 0;
        while (i < normalized.size()) {
            if (normalized[i] == ' ') {
                ++i;
                continue;
            }
            const size_t start = i;
            while (i < normalized.size() && normalized[i] != ' ') {
                ++i;
            }
            const std::string_view word = normalized.substr(start, i - start);
            if (word.size() >= kMinWordToCorrect && std::ranges::none_of(word, IsDigit)) {
                ++counts_[std::string(word)];
                wordsStale_ = true;
            }
        }
        lastTyped_.reset();
    }

    std::optional<std::string> Suggester::Correction_(const std::string& word) const {
        const int maxDistance = word.size() <= 4 ? 1 : 2;
        int bestDistance = maxDistance + 1;
        const std::string* best = nullptr;
        for (const auto& [candidate, count] : words_) {
            if (candidate.find(word) != std::string::npos) {
                return std::nullopt;  // known, also as part of a longer word
            }
            const auto lengthGap = static_cast<int>(candidate.size()) - static_cast<int>(word.size());
            if (std::abs(lengthGap) > maxDistance) {
                continue;
            }
            // The most common word comes first, so it wins a tie.
            if (const int distance = EditDistance(word, candidate); distance < bestDistance) {
                bestDistance = distance;
                best = &candidate;
            }
        }
        return best ? std::optional<std::string>(*best) : std::nullopt;
    }

    std::optional<std::string> Suggester::Suggest(const std::string_view typed) {
        if (lastTyped_ && *lastTyped_ == typed) {
            return lastResult_;
        }
        if (wordsStale_) {
            words_.assign(counts_.begin(), counts_.end());
            std::ranges::sort(words_, [](const auto& a, const auto& b) {
                return a.second != b.second ? a.second > b.second : a.first < b.first;
            });
            wordsStale_ = false;
        }

        std::string result;
        bool changed = false;
        size_t i = 0;
        while (i < typed.size()) {
            if (!IsWordChar(typed[i])) {
                result += typed[i++];
                continue;
            }
            const size_t start = i;
            std::string word;
            bool hasDigit = false;
            while (i < typed.size() && IsWordChar(typed[i])) {
                hasDigit = hasDigit || IsDigit(typed[i]);
                word += Lower(typed[i++]);
            }

            const bool excluded = start > 0 && typed[start - 1] == '-' && (start == 1 || IsSpace(typed[start - 2]));
            std::optional<std::string> correction;
            if (!excluded && !hasDigit && word.size() >= kMinWordToCorrect) {
                correction = Correction_(word);
            }
            if (correction) {
                result += *correction;
                changed = true;
            }
            else {
                result += typed.substr(start, i - start);
            }
        }

        lastTyped_ = std::string(typed);
        lastResult_ = changed ? std::optional<std::string>(std::move(result)) : std::nullopt;
        return lastResult_;
    }
}
