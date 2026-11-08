#pragma once

#include <filesystem>
#include <optional>
#include <string>

struct DetectedPath {
    std::filesystem::path path;
    std::string source; // Where the path came from, for logging.
};

struct DetectedPluginPaths {
    std::optional<DetectedPath> gameRoot;
    std::optional<DetectedPath> userPluginsRoot;
    std::optional<std::filesystem::path> winePrefix; // Prefix the paths were found in, if any.
};

// Directory containing the running executable, or an empty path if it cannot be determined.
auto GetExecutableDirectory(const char* argv0) -> std::filesystem::path;

// Finds the default SimCity 4 game root and user Plugins directory.
// Windows: settings saved by the installer, the Maxis registry key and the Documents known folder.
// Elsewhere: a Wine prefix (the given one, $WINEPREFIX, or ~/.wine and Steam Proton prefixes) and Steam libraries.
auto DetectPluginPaths(const std::optional<std::filesystem::path>& winePrefix) -> DetectedPluginPaths;

// Matches path components case-insensitively where the exact spelling does not exist, like Windows and Wine do.
// Returns the path unchanged when it already exists, cannot be resolved, or on Windows.
auto ResolvePathCaseInsensitive(const std::filesystem::path& path) -> std::filesystem::path;
