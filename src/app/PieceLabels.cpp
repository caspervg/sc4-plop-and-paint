#include "PieceLabels.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace {
    // The share of a button's names that a word must be in before it counts as shared by the button.
    constexpr double kSharedShare = 0.3;

    // Words that tie a name together: never dropped, and the word after one is what the relation is about.
    const std::unordered_set<std::string> kRelation = {"over", "under", "x",       "with", "on",  "to",
                                                       "in",   "above", "into",    "without", "no", "not"};
    // Words that only join others. A label does not start or end with one.
    const std::unordered_set<std::string> kConjunction = {"and", "for", "of", "the", "&", "or"};

    struct Rewrite {
        std::regex pattern;
        const char* replacement;
    };

    // Step 1 of the labels, in this order. NAM names spell networks many ways; one spelling makes them one
    // word, so that the words a button shares are found.
    const std::vector<Rewrite>& Rewrites() {
        static const std::vector<Rewrite> rewrites = [] {
            const std::pair<const char*, const char*> rules[] = {
                // Usage notes at the end tell how to use the piece, not which piece it is.
                {R"([\s(,-]+(?:can be overridden|rotate for|overrides? for|works for|place with|use (?:with|adjacent)|\(?works with).*$)",
                 ""},
                {R"(\bpuzzle\s+pieces?\b)", ""},
                {R"(\bpieces?\b)", ""},
                {R"(\(\s*\d+\s*x\s*\d+\s*\))", ""},  // the size, which the tile's badge shows
                {R"(\bone[\s-]?way[\s-]?roads?\b)", "OWR"},
                {R"(\boneway\b)", "OWR"},
                {R"(\bground\s*light\s*rail\b)", "GLR"},
                {R"(\belevated\s*light\s*rail\b)", "El-Rail"},
                {R"(\belevated\s*rail\b|\bel-?rail\b)", "El-Rail"},
                {R"(\bhigh\s+speed\s+rail\b)", "HSR"},
                {R"(\bground\s*highway\b)", "GHwy"},
                {R"(\belevated\s+highway\b)", "ElHwy"},
                {R"(\bped(?:estrian)?\s*-?\s*malls?\b)", "Pedmall"},
                {R"(\breal\s*highway\b)", "RHW"},
                {R"(\breal\s*expressway\b)", "REW"},
                {R"(\bfractional(?:ly)?[\s-]+angled?\b(?:\s*\(FA\))?)", "FA"},
                {R"(\btram-avenue\b)", "Tram-in-Avenue"},  // as the NAM calls it, like Tram-in-Road
                {R"(\bavenue\b)", "Ave"},
                {R"(\bdiagonal\b)", "Diag"},
                {R"(\borth[oa]gonal\b)", "Ortho"},
                {R"(\bperpendicular\b)", "Perp"},
                {R"(\s*-?\s*deg(?:ree)?s?\b)", "\xC2\xB0"},  // 90-Degree: 90°
                {R"(\bintersections?\b)", "Int"},
                {R"(\bjunctions?\b)", "Jct"},
                {R"(\btransitions?\b)", "Trans"},
                {R"(\belevated\b)", "Elev"},
                {R"(\blevel\s*(\d)\b)", "L$1"},
                {R"((\s)-(?=\S))", "$1- "},  // "Block Markings -Exit Arrow": the dash is a separator
            };
            std::vector<Rewrite> result;
            for (const auto& [pattern, replacement] : rules) {
                result.push_back({std::regex(pattern, std::regex::ECMAScript | std::regex::icase), replacement});
            }
            return result;
        }();
        return rewrites;
    }

    bool IsSpace(const char c) {
        return std::isspace(static_cast<unsigned char>(c)) != 0;
    }

    std::string Lower(std::string_view text) {
        std::string lower(text);
        for (char& c : lower) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return lower;
    }

    // The word as the counting sees it: lower case, without brackets and stops around it.
    std::string Key(std::string_view word) {
        constexpr std::string_view kTrim = "(),.:;";
        while (!word.empty() && kTrim.find(word.front()) != std::string_view::npos) {
            word.remove_prefix(1);
        }
        while (!word.empty() && kTrim.find(word.back()) != std::string_view::npos) {
            word.remove_suffix(1);
        }
        return Lower(word);
    }

    // A word that only separates: "-", "|", ":".
    bool IsSeparator(const std::string_view word) {
        return !word.empty() && std::ranges::all_of(word, [](const char c) {
            return std::strchr("-|:,/+.;", c) != nullptr;
        });
    }

    std::vector<std::string> Words(const std::string_view text) {
        std::vector<std::string> words;
        size_t i = 0;
        while (i < text.size()) {
            while (i < text.size() && IsSpace(text[i])) {
                ++i;
            }
            const size_t start = i;
            while (i < text.size() && !IsSpace(text[i])) {
                ++i;
            }
            if (i > start) {
                words.emplace_back(text.substr(start, i - start));
            }
        }
        return words;
    }

    std::string Join(const std::vector<std::string>& words) {
        std::string text;
        for (const std::string& word : words) {
            text += (text.empty() ? "" : " ") + word;
        }
        return text;
    }

    std::string Capitalized(std::string text) {
        if (!text.empty()) {
            text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
        }
        return text;
    }

    // Step 4: no separators inside, no conjunction at either end, no relation word at the end.
    std::string Tidy(const std::vector<std::string>& words) {
        std::vector<std::string> kept;
        for (std::string word : words) {
            while (!word.empty() && std::strchr(",.;", word.back()) != nullptr) {
                word.pop_back();
            }
            if (!word.empty() && !IsSeparator(word)) {
                kept.push_back(std::move(word));
            }
        }
        while (!kept.empty() && kConjunction.contains(Key(kept.front()))) {
            kept.erase(kept.begin());
        }
        while (!kept.empty() && (kConjunction.contains(Key(kept.back())) || kRelation.contains(Key(kept.back())))) {
            kept.pop_back();
        }
        return Capitalized(Join(kept));
    }

    // Steps 3 and 4 for every name: the names without the dropped words.
    std::vector<std::string> WithoutWords(const std::vector<std::vector<std::string>>& names,
                                          const std::unordered_set<std::string>& drop) {
        std::vector<std::string> labels;
        labels.reserve(names.size());
        for (const auto& words : names) {
            // The word after a relation word stays, unless it ends the name: "to Tram-in-Road", "over Road".
            std::vector<bool> isObject(words.size(), false);
            for (size_t i = 1; i + 1 < words.size(); ++i) {
                isObject[i] = kRelation.contains(Key(words[i - 1]));
            }
            std::vector<std::string> kept;
            bool ownWord = false;
            for (size_t i = 0; i < words.size(); ++i) {
                const std::string key = Key(words[i]);
                if (!drop.contains(key) || isObject[i]) {
                    kept.push_back(words[i]);
                    ownWord = ownWord || (!isObject[i] && !kRelation.contains(key) && !kConjunction.contains(key) &&
                                          !IsSeparator(words[i]));
                }
            }
            if (!ownWord) {
                // Every word that tells this piece apart is one the button shares: only cut the shared start.
                size_t start = 0;
                while (start < words.size() && (drop.contains(Key(words[start])) || IsSeparator(words[start]))) {
                    ++start;
                }
                kept.assign(words.begin() + static_cast<ptrdiff_t>(start), words.end());
            }
            labels.push_back(Tidy(kept));
        }
        return labels;
    }

    size_t DistinctCount(const std::vector<std::string>& texts) {
        std::unordered_set<std::string> seen;
        for (const std::string& text : texts) {
            seen.insert(Lower(text));
        }
        return seen.size();
    }

    // The words of the names, each counted once per name, most common first. Words as common as each
    // other keep the order they first come in.
    std::vector<std::pair<std::string, size_t>> WordCounts(const std::vector<std::vector<std::string>>& names) {
        std::vector<std::pair<std::string, size_t>> counts;
        std::unordered_map<std::string, size_t> at;
        for (const auto& words : names) {
            std::unordered_set<std::string> seen;
            for (const std::string& word : words) {
                std::string key = Key(word);
                if (!seen.insert(key).second) {
                    continue;
                }
                if (const auto [it, isNew] = at.try_emplace(key, counts.size()); isNew) {
                    counts.emplace_back(std::move(key), 1);
                }
                else {
                    ++counts[it->second].second;
                }
            }
        }
        std::ranges::stable_sort(counts, std::greater{}, &std::pair<std::string, size_t>::second);
        return counts;
    }

    // Groups of the indices whose texts are the same apart from case, in the order they first come in.
    std::vector<std::vector<size_t>> SameGroups(const std::vector<std::string>& texts) {
        std::vector<std::vector<size_t>> groups;
        std::unordered_map<std::string, size_t> at;
        for (size_t i = 0; i < texts.size(); ++i) {
            const auto [it, isNew] = at.try_emplace(Lower(texts[i]), groups.size());
            if (isNew) {
                groups.emplace_back();
            }
            groups[it->second].push_back(i);
        }
        return groups;
    }

    // The rules for button names, in this order. Each NAM project goes by one name, the short one that RHW,
    // NWM and FLUPs already go by in most buttons, so the buttons of a project sort together and read alike.
    const std::vector<Rewrite>& ButtonRewrites() {
        static const std::vector<Rewrite> rewrites = [] {
            const std::pair<const char*, const char*> rules[] = {
                {R"(_)", " "},     // "Avenue_Roundabouts"
                {R"(--)", " - "},  // "Draggable Ground Light Rail (Tram)--Additional Styles"
                {R"(\breal\s*highway\b(?:\s*\(RHW\))?)", "RHW"},
                {R"(\breal\s*railway\b(?:\s*\(RRW\))?)", "RRW"},
                {R"(\breal\s*expressway\b(?:\s*\(REW\))?)", "REW"},
                {R"(\bnetwork widening mod\b(?:\s*\(NWM\))?)", "NWM"},
                {R"(\btram-FLUPs\s*-\s*flexible underpasses\b)", "Tram FLUPs"},
                {R"(\bflexible underpasses\b(?:\s*\(FLUPs\))?)", "FLUPs"},
                {R"(\bpedestrian revolution mod\b(?:\s*\(PRM\))?)", "PRM"},
                {R"(\bstreet addon mod\b(?:\s*\(SAM\))?)", "SAM"},
                {R"(\bmulti-radius curve\b(?:\s*\(MRC\))?)", "MRC"},
                {R"(\bground high speed rail\b(?:\s*\(GHSR\))?)", "GHSR"},
                {R"(\bhigh speed rail\b(?:\s*\(HSR\))?)", "HSR"},
                {R"(\bsingle-track rail\b(?:\s*\(STR\))?)", "STR"},
                {R"(\bcanal\s+addon\s+mod\b(?:\s*\(CAN-AM\))?)", "CAN-AM"},
                // The trams name their network once: "Tram-in-Avenue", like "Tram-in-Road".
                {R"(\btram-avenue\b)", "Tram-in-Avenue"},
                {R"(\s*\(\s*ground\s*light\s*rail\s*/\s*avenue\s*\)\s*-\s*dual\s+network\s+pieces\b)", ""},
                {R"(\belevated\s*rail\b|\bel-?rail\b)", "El-Rail"},
                {R"(\bground\s*light\s*rail\b)", "GLR"},
                {R"(\bfractional(?:ly)?[\s-]+angled?\b(?:\s*\(FA\))?)", "FA"},
                // Every button holds pieces: "Puzzle Pieces" and "Network Pieces" say no more than "Pieces".
                {R"(\s*\(\s*puzzle\s+pieces\s*\))", ""},
                {R"(\b(?:network|puzzle)\s+pieces\b)", "Pieces"},
            };
            std::vector<Rewrite> result;
            for (const auto& [pattern, replacement] : rules) {
                result.push_back({std::regex(pattern, std::regex::ECMAScript | std::regex::icase), replacement});
            }
            return result;
        }();
        return rewrites;
    }

    // A button name as the list shows it: "RealHighway (RHW) Starter Pieces" becomes "RHW Starter Pieces",
    // "RealRailway FLEX Curve Pieces." becomes "RRW FLEX Curve Pieces".
    std::string TidyButtonName(const std::string_view name) {
        std::string text(name);
        for (const Rewrite& rewrite : ButtonRewrites()) {
            text = std::regex_replace(text, rewrite.pattern, rewrite.replacement);
        }
        text = Join(Words(text));
        while (!text.empty() && (text.back() == '.' || IsSpace(text.back()))) {
            text.pop_back();
        }
        return text.empty() ? std::string(name) : text;
    }
}

std::string PieceLabels::Normalize(const std::string_view name) {
    std::string text(name);
    for (const Rewrite& rewrite : Rewrites()) {
        text = std::regex_replace(text, rewrite.pattern, rewrite.replacement);
    }
    for (char& c : text) {
        if (c == '(' || c == ')' || c == '[' || c == ']') {
            c = ' ';
        }
    }
    // A word written twice in a row, such as "RHW RHW" from "Real Highway (RHW)", is written once.
    std::vector<std::string> words;
    for (std::string& word : Words(text)) {
        if (words.empty() || Lower(word) != Lower(words.back())) {
            words.push_back(std::move(word));
        }
    }
    text = Join(words);
    const size_t start = text.find_first_not_of(" .");
    const size_t end = text.find_last_not_of(" .");
    return start == std::string::npos ? std::string() : text.substr(start, end - start + 1);
}

std::vector<std::string> PieceLabels::Make(const std::span<const Piece> pieces) {
    std::vector<std::string> normalized;
    std::vector<std::vector<std::string>> names;
    for (const Piece& piece : pieces) {
        normalized.push_back(Normalize(piece.name));
        names.push_back(Words(normalized.back()));
    }

    // Step 2: drop the words the button shares, as long as the names stay apart.
    const size_t distinct = DistinctCount(normalized);
    const double shared = std::max(2.0, kSharedShare * static_cast<double>(pieces.size()));
    std::unordered_set<std::string> drop;
    for (const auto& [word, count] : WordCounts(names)) {
        if (static_cast<double>(count) < shared) {
            break;
        }
        if (kRelation.contains(word) || IsSeparator(word)) {
            continue;
        }
        drop.insert(word);
        const std::vector<std::string> trial = WithoutWords(names, drop);
        if (DistinctCount(trial) < distinct ||
            std::ranges::any_of(trial, [](const std::string& label) { return label.size() < 2; })) {
            drop.erase(word);
        }
    }
    std::vector<std::string> labels = WithoutWords(names, drop);
    for (size_t i = 0; i < labels.size(); ++i) {
        if (labels[i].empty()) {
            labels[i] = normalized[i];
        }
    }

    // Step 5: pieces with the same label, mostly pieces with the same name, apart by orientation, then by count.
    for (const auto& group : SameGroups(labels)) {
        if (group.size() < 2) {
            continue;
        }
        const bool allKnown = std::ranges::none_of(group, [&](const size_t i) {
            return pieces[i].orientation == Orientation::Unknown;
        });
        const bool differ = std::ranges::any_of(group, [&](const size_t i) {
            return pieces[i].orientation != pieces[group.front()].orientation;
        });
        if (allKnown && differ) {
            for (const size_t i : group) {
                labels[i] += pieces[i].orientation == Orientation::Diagonal ? " Diag" : " Ortho";
            }
        }
        std::vector<std::string> sub;
        for (const size_t i : group) {
            sub.push_back(labels[i]);
        }
        for (const auto& same : SameGroups(sub)) {
            for (size_t k = 1; k < same.size(); ++k) {
                labels[group[same[k]]] += " #" + std::to_string(k + 1);
            }
        }
    }
    return labels;
}

std::vector<std::string> PieceLabels::ButtonLabels(const std::span<const Button> buttons) {
    std::vector<std::string> labels;
    for (const Button& button : buttons) {
        labels.push_back(TidyButtonName(button.name));
    }

    for (const auto& group : SameGroups(labels)) {
        if (group.size() < 2) {
            continue;
        }
        std::unordered_set<std::string> ownWords;
        for (const std::string& word : Words(Normalize(buttons[group.front()].name))) {
            ownWords.insert(Key(word));
        }
        // For each button of the group: the share of its pieces that have each word, and the word as it is
        // first written, in the order the words first come in.
        struct ButtonWords {
            std::vector<std::string> order;
            std::unordered_map<std::string, double> share;
            std::unordered_map<std::string, std::string> written;
        };
        std::vector<ButtonWords> words(group.size());
        for (size_t g = 0; g < group.size(); ++g) {
            const Button& button = buttons[group[g]];
            std::vector<std::vector<std::string>> names;
            for (const std::string_view name : button.pieceNames) {
                names.push_back(Words(Normalize(name)));
            }
            for (const auto& [key, count] : WordCounts(names)) {
                words[g].share[key] = static_cast<double>(count) / static_cast<double>(names.size());
            }
            for (const auto& name : names) {
                for (const std::string& word : name) {
                    std::string key = Key(word);
                    if (words[g].written.try_emplace(key, word).second) {
                        words[g].order.push_back(std::move(key));
                    }
                }
            }
        }
        for (size_t g = 0; g < group.size(); ++g) {
            // A word scores by how much more of this button's pieces have it than those of any namesake.
            std::vector<std::pair<double, size_t>> scored;  // score, position in order
            for (size_t at = 0; at < words[g].order.size(); ++at) {
                const std::string& key = words[g].order[at];
                if (key.size() < 2 || kRelation.contains(key) || kConjunction.contains(key) || IsSeparator(key) ||
                    ownWords.contains(key)) {
                    continue;
                }
                double other = 0.0;
                for (size_t h = 0; h < group.size(); ++h) {
                    if (h != g) {
                        if (const auto share = words[h].share.find(key); share != words[h].share.end()) {
                            other = std::max(other, share->second);
                        }
                    }
                }
                scored.emplace_back(words[g].share[key] - other, at);
            }
            std::ranges::stable_sort(scored, std::greater{}, &std::pair<double, size_t>::first);
            std::string suffix;
            if (!scored.empty() && scored.front().first >= 0.15) {
                // The best word, and up to two more that score nearly as well, in the order they come in.
                const double cutoff = std::max(0.15, scored.front().first * 0.75) - 1e-9;
                std::vector<size_t> chosen;
                for (const auto& [score, at] : scored) {
                    if (score < cutoff || chosen.size() == 3) {
                        break;
                    }
                    chosen.push_back(at);
                }
                std::ranges::sort(chosen);
                for (const size_t at : chosen) {
                    std::string word = words[g].written[words[g].order[at]];
                    while (!word.empty() && std::strchr("(),.:;", word.back()) != nullptr) {
                        word.pop_back();
                    }
                    suffix += (suffix.empty() ? "" : ", ") + Capitalized(word);
                }
            }
            labels[group[g]] += suffix.empty() ? " (" + std::to_string(g + 1) + ")" : " \xC2\xB7 " + suffix;
        }
    }
    return labels;
}
