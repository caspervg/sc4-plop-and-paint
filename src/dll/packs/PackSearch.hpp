#pragma once

#include <algorithm>
#include <cctype>
#include <string_view>

// Whether every word of the query is in the text, in any order and ignoring case, so that
// "bsc mega props sg" finds "BSC MEGA Props - SG Vol01".
[[nodiscard]] inline bool ContainsAllWords(const std::string_view text, const std::string_view query) {
    const auto lower = [](const char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    size_t position = 0;
    while (position < query.size()) {
        if (query[position] == ' ') {
            ++position;
            continue;
        }
        const size_t end = std::min(query.find(' ', position), query.size());
        if (std::ranges::search(text, query.substr(position, end - position), {}, lower, lower).empty()) {
            return false;
        }
        position = end;
    }
    return true;
}
