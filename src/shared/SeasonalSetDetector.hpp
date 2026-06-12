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

namespace detail {

// Longer tokens first so e.g. "winter" wins over "win"-like prefixes of it.
inline constexpr std::array<std::string_view, 26> kSeasonTokens = {
    "september",   "december",    "november",   "february",  "seasonal", "leafless",
    "january",     "october",     "august",     "autumn",    "spring",   "summer",
    "winter",      "april",       "march",      "earlyfall", "latefall", "fall",
    "snow",        "bare",        "june",       "july",      "earlyspring",
    "latesummer",  "earlysummer", "latespring",
};

// Short month forms only strip when bounded by separators/digits, otherwise
// they shred regular words ("mar" in "marina", "jan" in "janitor").
inline constexpr std::array<std::string_view, 12> kShortMonthTokens = {
    "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec",
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

struct Candidate {
    const Prop* prop = nullptr;
    size_t sourceFile = 0;  // load-order index of the file the prop came from
    DateWindow window{};
    std::string stem;
};

// A set is plausible when its windows tile a decent chunk of the year. Overlap is
// allowed only where windows nest (Winter + Winter_Snow); independent overlap
// (e.g. two summer variants) disqualifies the grouping.
inline bool WindowsLookSeasonal(const std::vector<const Candidate*>& members) {
    std::array<uint8_t, 365> coverage{};
    for (const auto* member : members) {
        MarkWindow(member->window, coverage);
    }
    const auto covered = static_cast<int>(std::count_if(coverage.begin(), coverage.end(),
                                                        [](const uint8_t c) { return c > 0; }));
    if (covered < kMinYearCoverageDays) {
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
    return unexplainedOverlap <= 30;
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

    // Primary pass: cluster by (group ID, stem). std::map keeps output deterministic.
    std::map<std::pair<uint32_t, std::string>, std::vector<const Candidate*>> clusters;
    for (const auto& candidate : candidates) {
        clusters[{candidate.prop->groupId.value(), candidate.stem}].push_back(&candidate);
    }

    struct WorkingSet {
        std::vector<const Candidate*> members;
        std::string stem;
        uint8_t confidence = 0;
    };
    std::vector<WorkingSet> sets;
    std::vector<const Candidate*> singletons;

    for (auto& [key, members] : clusters) {
        if (members.size() >= 2 && members.size() <= kMaxSetMembers && WindowsLookSeasonal(members)) {
            sets.push_back(WorkingSet{std::move(members), key.second, 0});
        }
        else {
            singletons.insert(singletons.end(), members.begin(), members.end());
        }
    }

    // Rescue pass: attach singletons to an existing set (or another singleton) from
    // the same group with a long shared name prefix and a complementary window.
    const auto nameMatches = [](const Candidate& a, const Candidate& b, const bool sameFile) {
        const auto& nameA = a.prop->exemplarName;
        const auto& nameB = b.prop->exemplarName;
        const size_t prefix = CommonPrefixLength(nameA, nameB);
        const double required = sameFile ? 0.5 : 0.6;
        return prefix >= 6 && prefix >= static_cast<size_t>(static_cast<double>(std::min(nameA.size(), nameB.size())) * required);
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
