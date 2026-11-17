#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

// Converts a path to UTF-8 for logs and reports without ever throwing.
// path::string() converts to the ANSI code page on Windows and throws for names it cannot represent
// (Cyrillic or CJK names on a Western locale, emoji, ...), so it must not be used for plugin paths.
inline auto PathToUtf8(const std::filesystem::path& path) -> std::string {
#ifdef _WIN32
    const auto& native = path.native();
    std::string out;
    out.reserve(native.size());
    const auto append = [&out](const uint32_t codePoint) {
        if (codePoint < 0x80) {
            out.push_back(static_cast<char>(codePoint));
        }
        else if (codePoint < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
        else if (codePoint < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
        else {
            out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
    };
    // Windows file names are UTF-16 but may hold unpaired surrogates, which become U+FFFD.
    for (size_t i = 0; i < native.size(); ++i) {
        uint32_t codePoint = static_cast<uint16_t>(native[i]);
        if (codePoint >= 0xD800 && codePoint <= 0xDBFF && i + 1 < native.size()) {
            const uint32_t low = static_cast<uint16_t>(native[i + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        if (codePoint >= 0xD800 && codePoint <= 0xDFFF) {
            codePoint = 0xFFFD;
        }
        append(codePoint);
    }
    return out;
#else
    // POSIX names are already bytes, usually UTF-8; returning them never throws.
    return path.native();
#endif
}
