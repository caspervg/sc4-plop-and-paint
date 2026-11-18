#pragma once
// Groups timed props into seasonal sets (e.g. spring/summer/fall/winter variants
// of one tree) so they can be painted together at the same coordinates.
//
// Heuristics, validated against a ~51k prop catalog (~2.5k timed props):
//   1. Only props with a simulator date window longer than kMinSeasonDays count as
//      seasonal; shorter windows are holiday/event props (gnomes, fireworks, ...).
//   2. Primary pass: cluster by (group ID, name stem) where the stem is the
//      exemplar name with season tokens (summer, winter, autumn, month names, ...)
//      stripped. Catches ~60% of seasonal props.
//   3. Validation: a real set's date windows roughly tile the year. Clusters whose
//      members barely cover the year or overlap heavily are split back up.
//      (Nested overlap like girafe's Winter + Winter_Snow is tolerated.)
//   4. Rescue pass: remaining singletons join a set when they share the group ID,
//      a long common name prefix and a complementary (non-overlapping) window.
//      Same-source-file props get a longer reach, name-wise.
// Unmatched timed props simply stay singletons; they are often intentional
// (farm crops, seasonal decorations without counterparts).

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "entities.hpp"

namespace seasonal {

inline constexpr uint32_t kMinSeasonDays = 15;
inline constexpr size_t kMaxSetMembers = 12;  // monthly sets exist (e.g. weather effects)
inline constexpr int kMinYearCoverageDays = 150;
inline constexpr int kMaxRescueOverlapDays = 10;
// VIP_VV_Seasonal_Tree_001 has a Winter_Snow window that starts before Winter
// and so overlaps both Autumn and Winter by 40 days in total.
inline constexpr int kMaxUnexplainedOverlapDays = 45;

namespace detail {

// Longer tokens first so e.g. "winter" wins over "win"-like prefixes of it.
// "autum" is a common misspelling (JRJ_T_AcerPlatanoid_01V2_AUTUM_S).
inline constexpr std::array<std::string_view, 27> kSeasonTokens = {
    "september",   "december",    "november",   "february",  "seasonal", "leafless",
    "january",     "october",     "august",     "autumn",    "autum",    "spring",
    "summer",      "winter",      "april",      "march",     "earlyfall", "latefall",
    "fall",        "snow",        "bare",       "june",      "july",     "earlyspring",
    "latesummer",  "earlysummer", "latespring",
};

// Short month forms and non-English seasons only strip when bounded by
// separators/digits, otherwise they shred regular words ("mar" in "marina",
// "ete" in "street"). German: SFBT_Birke_Fruehling-big. French: R6_DDE_Stock_Sel_Hiver.
inline constexpr std::array<std::string_view, 21> kShortMonthTokens = {
    "jan",       "feb",      "mar",    "apr",    "may",       "jun",     "jul",
    "aug",       "sep",      "oct",    "nov",    "dec",       "fruehling", "fruhling",
    "sommer",    "herbst",   "printemps", "ete", "automne",   "hiver",   "saison",
};

// Words that only mark a variant, for the rescue pass name comparison: short season
// forms (fz_redcabbSPRI / fz_redcabbSUM), growth stages (Grfe_wheat_blade,
// PEG-MTPA_Crop_Barley_Young / _Mature, fz_CP_Lodgepole3_stump1) and holiday
// dressing (JENXPROP_Tree1_Xmas). Whole words only, unlike kSeasonTokens.
inline constexpr std::array<std::string_view, 19> kVariantWords = {
    "spr",   "spri",   "sum",   "summ",  "aut",     "autu",    "win", "wint",   "fal", "xmas",
    "young", "mature", "blade", "bloom", "blossom", "stubble", "cut", "flower", "stump",
};

inline bool IsSeparator(const char c) {
    return c == '_' || c == '-' || c == ' ' || std::isdigit(static_cast<unsigned char>(c)) != 0;
}

inline std::string ToLower(const std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Strips season tokens and collapses separators. Returns the lowercase stem.
inline std::string Stem(const std::string_view name) {
    const std::string lower = ToLower(name);
    std::string stripped;
    stripped.reserve(lower.size());

    size_t i = 0;
    while (i < lower.size()) {
        bool matched = false;
        for (const auto token : kSeasonTokens) {
            if (lower.compare(i, token.size(), token) == 0) {
                i += token.size();
                matched = true;
                break;
            }
        }
        if (!matched) {
            for (const auto token : kShortMonthTokens) {
                if (lower.compare(i, token.size(), token) != 0) {
                    continue;
                }
                const bool boundedLeft = i == 0 || IsSeparator(lower[i - 1]);
                const size_t end = i + token.size();
                const bool boundedRight = end >= lower.size() || IsSeparator(lower[end]);
                if (boundedLeft && boundedRight) {
                    i = end;
                    matched = true;
                    break;
                }
            }
        }
        if (!matched) {
            stripped.push_back(lower[i]);
            ++i;
        }
    }

    // Collapse separator runs to single underscores and trim them.
    std::string stem;
    stem.reserve(stripped.size());
    for (const char c : stripped) {
        if (c == '_' || c == '-' || c == ' ') {
            if (!stem.empty() && stem.back() != '_') {
                stem.push_back('_');
            }
        }
        else {
            stem.push_back(c);
        }
    }
    while (!stem.empty() && stem.back() == '_') {
        stem.pop_back();
    }
    return stem;
}

inline int DayOfYear(const uint8_t month, const uint8_t day) {
    static constexpr std::array<int, 12> kCumulative = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    const int m = std::clamp(static_cast<int>(month), 1, 12);
    const int d = std::clamp(static_cast<int>(day), 1, 31);
    return kCumulative[static_cast<size_t>(m - 1)] + d - 1;
}

struct DateWindow {
    int startDay = 0;  // day of year, 0-based
    int duration = 0;  // clamped to a full year
};

inline DateWindow WindowOf(const Prop& prop) {
    const auto& start = *prop.simulatorDateStart;
    const int duration = std::min(static_cast<int>(prop.simulatorDateDuration.value_or(0)), 365);
    return DateWindow{DayOfYear(start.month, start.day), duration};
}

inline void MarkWindow(const DateWindow& window, std::array<uint8_t, 365>& coverage) {
    for (int i = 0; i < window.duration; ++i) {
        auto& count = coverage[static_cast<size_t>((window.startDay + i) % 365)];
        if (count < 255) {
            ++count;
        }
    }
}

inline int OverlapDays(const DateWindow& a, const DateWindow& b) {
    std::array<bool, 365> inA{};
    for (int i = 0; i < a.duration; ++i) {
        inA[static_cast<size_t>((a.startDay + i) % 365)] = true;
    }
    int overlap = 0;
    for (int i = 0; i < b.duration; ++i) {
        if (inA[static_cast<size_t>((b.startDay + i) % 365)]) {
            ++overlap;
        }
    }
    return overlap;
}

inline size_t CommonPrefixLength(const std::string_view a, const std::string_view b) {
    const size_t limit = std::min(a.size(), b.size());
    size_t n = 0;
    while (n < limit &&
           std::tolower(static_cast<unsigned char>(a[n])) == std::tolower(static_cast<unsigned char>(b[n]))) {
        ++n;
    }
    return n;
}

inline bool IsVariantWord(const std::string_view word) {
    return std::find(kVariantWords.begin(), kVariantWords.end(), word) != kVariantWords.end();
}

// Splits a stem into words: at separators, between letters and digits, and before a
// trailing variant word glued to a longer word ("redcabbspri" -> "redcabb", "spri").
inline std::vector<std::string_view> Words(const std::string_view stem) {
    const auto isAlnum = [](const char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; };
    const auto isDigit = [](const char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; };
    std::vector<std::string_view> words;
    size_t i = 0;
    while (i < stem.size()) {
        if (!isAlnum(stem[i])) {
            ++i;
            continue;
        }
        const bool digits = isDigit(stem[i]);
        size_t end = i;
        while (end < stem.size() && isAlnum(stem[end]) && isDigit(stem[end]) == digits) {
            ++end;
        }
        std::string_view word = stem.substr(i, end - i);
        if (!digits) {
            for (const auto variant : kVariantWords) {
                if (word.size() >= variant.size() + 3 && word.ends_with(variant)) {
                    words.push_back(word.substr(0, word.size() - variant.size()));
                    word = variant;
                    break;
                }
            }
        }
        words.push_back(word);
        i = end;
    }
    return words;
}

// True when two stems differ only in variant markers: numbers, one- or two-letter
// codes and kVariantWords. Rejects names that share a prefix but name different
// things (SG_FarmField_SugarCane03 vs SG_FarmField_Tobacco).
inline bool DifferOnlyInVariants(const std::string_view stemA, const std::string_view stemB) {
    const auto a = Words(stemA);
    const auto b = Words(stemB);
    size_t prefix = 0;
    while (prefix < a.size() && prefix < b.size() && a[prefix] == b[prefix]) {
        ++prefix;
    }
    if (prefix == 0) {
        return false;
    }
    size_t suffix = 0;
    while (suffix < a.size() - prefix && suffix < b.size() - prefix &&
           a[a.size() - 1 - suffix] == b[b.size() - 1 - suffix]) {
        ++suffix;
    }
    // One word grown into a longer one is a typo or a qualifier, not another
    // plant: CP_SHADED_Seas_Maple9a_51Prop_Fall next to MapleShaded9a_51Prop_Winter.
    if (a.size() - prefix - suffix == 1 && b.size() - prefix - suffix == 1) {
        const std::string_view wordA = a[prefix];
        const std::string_view wordB = b[prefix];
        if (std::min(wordA.size(), wordB.size()) >= 4 && (wordA.starts_with(wordB) || wordB.starts_with(wordA))) {
            return true;
        }
    }
    // Up to three letters covers variant codes like A, B2 or JRJ's SS / SSU / SA.
    const auto isMarker = [](const std::string_view word) {
        return std::isdigit(static_cast<unsigned char>(word.front())) != 0 || word.size() <= 3 || IsVariantWord(word);
    };
    const auto onlyMarkers = [&](const std::vector<std::string_view>& words) {
        return std::all_of(words.begin() + static_cast<std::ptrdiff_t>(prefix),
                           words.end() - static_cast<std::ptrdiff_t>(suffix), isMarker);
    };
    return onlyMarkers(a) && onlyMarkers(b);
}

struct Candidate {
    const Prop* prop = nullptr;
    size_t sourceFile = 0;  // load-order index of the file the prop came from
    DateWindow window{};
    std::string stem;
};

// A set is plausible when its windows tile a decent chunk of the year. Overlap is
// allowed only where windows nest (Winter + Winter_Snow); independent overlap
// (e.g. two summer variants) disqualifies the grouping.
inline int CoveredDays(const std::vector<const Candidate*>& members) {
    std::array<uint8_t, 365> coverage{};
    for (const auto* member : members) {
        MarkWindow(member->window, coverage);
    }
    return static_cast<int>(std::count_if(coverage.begin(), coverage.end(), [](const uint8_t c) { return c > 0; }));
}

inline bool WindowsLookSeasonal(const std::vector<const Candidate*>& members) {
    if (CoveredDays(members) < kMinYearCoverageDays) {
        return false;
    }

    // Count overlap that is NOT explained by one window nesting inside another.
    int unexplainedOverlap = 0;
    for (size_t i = 0; i < members.size(); ++i) {
        for (size_t j = i + 1; j < members.size(); ++j) {
            const int overlap = OverlapDays(members[i]->window, members[j]->window);
            const int smaller = std::min(members[i]->window.duration, members[j]->window.duration);
            if (overlap > 0 && overlap < smaller) {
                unexplainedOverlap += overlap;
            }
        }
    }
    return unexplainedOverlap <= kMaxUnexplainedOverlapDays;
}

inline std::string PrettifyStem(const std::string_view stem) {
    std::string pretty;
    pretty.reserve(stem.size());
    bool newWord = true;
    for (const char c : stem) {
        if (c == '_') {
            pretty.push_back(' ');
            newWord = true;
        }
        else {
            pretty.push_back(newWord ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c);
            newWord = false;
        }
    }
    return pretty;
}

}  // namespace detail

// True when the prop has a simulator date window covering `dayOfYear`
// (0-based, wrapping across new year). False for props without a window.
inline bool WindowContainsDay(const Prop& prop, const int dayOfYear) {
    if (!prop.simulatorDateStart.has_value()) {
        return false;
    }
    const detail::DateWindow window = detail::WindowOf(prop);
    if (window.duration <= 0) {
        return false;
    }
    const int offset = ((dayOfYear - window.startDay) % 365 + 365) % 365;
    return offset < window.duration;
}

// `sourceFileIndices` is parallel to `props` (load-order index of the originating
// file per prop); pass an empty vector when unknown.
inline std::vector<SeasonalSet> DetectSeasonalSets(const std::vector<Prop>& props,
                                                   const std::vector<size_t>& sourceFileIndices) {
    using namespace detail;

    const bool hasSources = sourceFileIndices.size() == props.size();

    std::vector<Candidate> candidates;
    for (size_t i = 0; i < props.size(); ++i) {
        const auto& prop = props[i];
        if (!prop.simulatorDateStart.has_value() ||
            prop.simulatorDateDuration.value_or(0) < kMinSeasonDays) {
            continue;
        }
        candidates.push_back(Candidate{
            &prop,
            hasSources ? sourceFileIndices[i] : 0,
            WindowOf(prop),
            Stem(prop.exemplarName),
        });
    }

    // Primary pass: cluster by stem. std::map keeps output deterministic.
    // The key ignores separators, since authors mix them within one set
    // (CP_Seas_Apple1_14_Prop_Spring next to CP_Seas_Apple1_14Prop_Summer).
    std::map<std::string, std::vector<const Candidate*>> clusters;
    for (const auto& candidate : candidates) {
        std::string key = candidate.stem;
        std::erase(key, '_');
        clusters[std::move(key)].push_back(&candidate);
    }

    struct WorkingSet {
        std::vector<const Candidate*> members;
        std::string stem;
        uint8_t confidence = 0;
    };
    std::vector<WorkingSet> sets;
    std::vector<const Candidate*> singletons;

    // A cluster may span group IDs: Grfe_beech_A_autumn ships in another group than
    // its summer and winter models. Members from different groups must fill each
    // other's gaps, so two creators' Oak_Summer + Oak_Winter stay apart.
    const auto groupsComplement = [](const std::vector<const Candidate*>& members) {
        for (size_t i = 0; i < members.size(); ++i) {
            for (size_t j = i + 1; j < members.size(); ++j) {
                if (members[i]->prop->groupId.value() != members[j]->prop->groupId.value() &&
                    OverlapDays(members[i]->window, members[j]->window) > kMaxRescueOverlapDays) {
                    return false;
                }
            }
        }
        return true;
    };
    const auto addCluster = [&](std::vector<const Candidate*> members) {
        if (members.size() >= 2 && members.size() <= kMaxSetMembers && WindowsLookSeasonal(members)) {
            std::string stem = members.front()->stem;
            sets.push_back(WorkingSet{std::move(members), std::move(stem), 0});
        }
        else {
            singletons.insert(singletons.end(), members.begin(), members.end());
        }
    };

    for (auto& [key, members] : clusters) {
        if (groupsComplement(members)) {
            addCluster(std::move(members));
            continue;
        }
        std::map<uint32_t, std::vector<const Candidate*>> byGroup;
        for (const auto* member : members) {
            byGroup[member->prop->groupId.value()].push_back(member);
        }
        for (auto& [group, groupMembers] : byGroup) {
            addCluster(std::move(groupMembers));
        }
    }

    // Rescue pass: attach singletons to an existing set (or another singleton) from
    // the same group with a long shared name prefix, names that differ only in
    // variant markers and a complementary window.
    const auto nameMatches = [](const Candidate& a, const Candidate& b, const bool sameFile) {
        const auto& nameA = a.prop->exemplarName;
        const auto& nameB = b.prop->exemplarName;
        const size_t prefix = CommonPrefixLength(nameA, nameB);
        const double required = sameFile ? 0.5 : 0.6;
        return prefix >= 6 &&
               prefix >= static_cast<size_t>(static_cast<double>(std::min(nameA.size(), nameB.size())) * required) &&
               DifferOnlyInVariants(a.stem, b.stem);
    };
    const auto windowFits = [](const Candidate& candidate, const std::vector<const Candidate*>& members) {
        return std::all_of(members.begin(), members.end(), [&](const Candidate* member) {
            return OverlapDays(candidate.window, member->window) <= kMaxRescueOverlapDays;
        });
    };

    std::vector<const Candidate*> unmatched;
    for (const auto* single : singletons) {
        WorkingSet* bestSet = nullptr;
        size_t bestPrefix = 0;
        for (auto& set : sets) {
            if (set.members.size() >= kMaxSetMembers ||
                set.members.front()->prop->groupId.value() != single->prop->groupId.value()) {
                continue;
            }
            const auto& representative = *set.members.front();
            const bool sameFile = single->sourceFile == representative.sourceFile;
            if (!nameMatches(*single, representative, sameFile) || !windowFits(*single, set.members)) {
                continue;
            }
            const size_t prefix = CommonPrefixLength(single->prop->exemplarName, representative.prop->exemplarName);
            if (prefix > bestPrefix) {
                bestPrefix = prefix;
                bestSet = &set;
            }
        }
        if (bestSet != nullptr) {
            bestSet->members.push_back(single);
            bestSet->confidence = 1;
            continue;
        }

        // No set fits; try pairing with an earlier unmatched singleton.
        bool paired = false;
        for (const auto* other : unmatched) {
            if (other->prop->groupId.value() != single->prop->groupId.value()) {
                continue;
            }
            const bool sameFile = single->sourceFile == other->sourceFile;
            if (!nameMatches(*single, *other, sameFile) ||
                OverlapDays(single->window, other->window) > kMaxRescueOverlapDays) {
                continue;
            }
            sets.push_back(WorkingSet{{other, single}, Stem(other->prop->exemplarName), 1});
            unmatched.erase(std::find(unmatched.begin(), unmatched.end(), other));
            paired = true;
            break;
        }
        if (!paired) {
            unmatched.push_back(single);
        }
    }

    // Rescue pairs skip WindowsLookSeasonal, so props that only share a name
    // prefix (SG_FarmField_SugarCane02 + SG_FarmField_Tobacco) can pair up and
    // leave the spot empty for most of the year. Drop those.
    std::erase_if(sets, [](const WorkingSet& set) { return CoveredDays(set.members) < kMinYearCoverageDays; });

    std::vector<SeasonalSet> result;
    result.reserve(sets.size());
    for (auto& set : sets) {
        std::sort(set.members.begin(), set.members.end(), [](const Candidate* a, const Candidate* b) {
            if (a->window.startDay != b->window.startDay) {
                return a->window.startDay < b->window.startDay;
            }
            return a->prop->instanceId.value() < b->prop->instanceId.value();
        });

        SeasonalSet out;
        out.name = PrettifyStem(set.stem);
        out.confidence = set.confidence;
        out.members.reserve(set.members.size());
        for (const auto* member : set.members) {
            out.members.push_back(member->prop->instanceId);
        }
        result.push_back(std::move(out));
    }

    std::sort(result.begin(), result.end(),
              [](const SeasonalSet& a, const SeasonalSet& b) { return a.name < b.name; });
    return result;
}

}  // namespace seasonal
