#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

inline uint64_t MakeGIKey(const uint32_t groupId, const uint32_t instanceId) {
    return (static_cast<uint64_t>(groupId) << 32) | instanceId;
}

inline std::string ToUpperCopy(std::string_view value) {
    std::string normalized(value);
    std::ranges::transform(normalized, normalized.begin(), [](const unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return normalized;
}

inline std::string ToLowerCopy(std::string_view value) {
    std::string normalized(value);
    std::ranges::transform(normalized, normalized.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return normalized;
}

inline bool ContainsCaseInsensitive(std::string_view text, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }

    return ToLowerCopy(text).contains(ToLowerCopy(needle));
}
