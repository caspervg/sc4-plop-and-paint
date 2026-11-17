#pragma once

#include <filesystem>
#include <string>

#include <fmt/std.h>

// Converts a path to UTF-8 for logs and reports without ever throwing.
// path::string() converts to the ANSI code page on Windows and throws for names it cannot represent
// (Cyrillic or CJK names on a Western locale, emoji, ...), so it must not be used for plugin paths.
// fmt converts Windows UTF-16 names to UTF-8, replacing unpaired surrogates, and passes POSIX names through.
inline auto PathToUtf8(const std::filesystem::path& path) -> std::string {
    return fmt::format("{}", path);
}
