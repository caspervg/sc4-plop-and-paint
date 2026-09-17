#include "PathDetection.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
// This translation unit does not include raylib, so the GDI/USER headers that shlobj.h needs are safe here.
#undef NOGDI
#undef NOUSER
#include <windows.h>
#include <shlobj.h>
#else
#include <fstream>
#include <iterator>
#include <utf8cpp/utf8.h>
#endif

namespace fs = std::filesystem;

namespace {
    constexpr auto kGameExecutable = "SimCity 4.exe";

    auto IsDirectory(const fs::path& path) -> bool {
        std::error_code ec;
        return !path.empty() && fs::is_directory(path, ec);
    }

    auto IsGameRoot(const fs::path& directory) -> bool {
        std::error_code ec;
        return IsDirectory(directory) &&
            fs::is_regular_file(ResolvePathCaseInsensitive(directory / "Apps" / kGameExecutable), ec);
    }

#ifdef _WIN32
    constexpr auto kDefaultInstallFolder = "SimCity 4 Deluxe Edition";

    auto GetKnownFolder(REFKNOWNFOLDERID folderId) -> fs::path {
        PWSTR rawPath = nullptr;
        fs::path result;
        if (SUCCEEDED(SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &rawPath)) && rawPath) {
            result = rawPath;
        }
        CoTaskMemFree(rawPath);
        return result;
    }

    auto ReadRegistryString(const HKEY root, const wchar_t* subKey, const wchar_t* valueName,
                            const REGSAM viewFlags) -> std::optional<fs::path> {
        HKEY key = nullptr;
        if (RegOpenKeyExW(root, subKey, 0, KEY_QUERY_VALUE | viewFlags, &key) != ERROR_SUCCESS) {
            return std::nullopt;
        }

        std::optional<fs::path> result;
        DWORD size = 0;
        if (RegGetValueW(key, nullptr, valueName, RRF_RT_REG_SZ, nullptr, nullptr, &size) == ERROR_SUCCESS &&
            size > sizeof(wchar_t)) {
            std::wstring value(size / sizeof(wchar_t), L'\0');
            if (RegGetValueW(key, nullptr, valueName, RRF_RT_REG_SZ, nullptr, value.data(), &size) == ERROR_SUCCESS) {
                value.resize(wcsnlen(value.c_str(), value.size()));
                if (!value.empty()) {
                    result = fs::path(value);
                }
            }
        }
        RegCloseKey(key);
        return result;
    }
#else
    auto EqualsIgnoreCase(const std::string_view a, const std::string_view b) -> bool {
        return std::ranges::equal(a, b, [](const unsigned char x, const unsigned char y) {
            return std::tolower(x) == std::tolower(y);
        });
    }

    auto GetEnvironmentPath(const char* name) -> std::optional<fs::path> {
        if (const char* value = std::getenv(name); value && value[0] != '\0') {
            return fs::path(value);
        }
        return std::nullopt;
    }

    auto StartsWithIgnoreCase(const std::string_view text, const std::string_view prefix) -> bool {
        return text.size() >= prefix.size() && EqualsIgnoreCase(text.substr(0, prefix.size()), prefix);
    }

    auto SortedSubdirectories(const fs::path& directory) -> std::vector<fs::path> {
        std::vector<fs::path> result;
        std::error_code ec;
        for (auto it = fs::directory_iterator(directory, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            if (IsDirectory(it->path())) {
                result.push_back(it->path());
            }
        }
        std::ranges::sort(result);
        return result;
    }

    // Decodes the quoted REG_SZ payload of a Wine .reg file line, e.g. C:\\Games\\SimCity 4" with \xHHHH escapes.
    auto DecodeWineRegistryString(const std::string_view encoded) -> std::string {
        std::string decoded;
        for (size_t i = 0; i < encoded.size(); ++i) {
            const char c = encoded[i];
            if (c == '"') {
                break;
            }
            if (c != '\\' || i + 1 >= encoded.size()) {
                decoded += c;
                continue;
            }

            const char escaped = encoded[++i];
            if (escaped != 'x') {
                decoded += escaped;
                continue;
            }

            uint32_t codePoint = 0;
            size_t digits = 0;
            while (digits < 4 && i + 1 < encoded.size() && std::isxdigit(static_cast<unsigned char>(encoded[i + 1]))) {
                const char hex = static_cast<char>(std::tolower(static_cast<unsigned char>(encoded[++i])));
                codePoint = codePoint * 16 + static_cast<uint32_t>(hex <= '9' ? hex - '0' : hex - 'a' + 10);
                ++digits;
            }
            if (digits == 0 || (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
                decoded += '?';
            }
            else {
                utf8::append(static_cast<char32_t>(codePoint), std::back_inserter(decoded));
            }
        }
        return decoded;
    }

    // Reads HKLM\Software\Maxis\SimCity 4 "Install Dir" from a prefix's system.reg.
    auto ReadWineInstallDir(const fs::path& prefix) -> std::optional<std::string> {
        std::ifstream file(prefix / "system.reg");
        if (!file) {
            return std::nullopt;
        }

        // 64-bit prefixes keep 32-bit application keys under Wow6432Node.
        constexpr std::array<std::string_view, 2> kKeys{
            R"([Software\\Wow6432Node\\Maxis\\SimCity 4])",
            R"([Software\\Maxis\\SimCity 4])",
        };
        constexpr std::string_view kValuePrefix = R"("Install Dir"=")";

        bool inKey = false;
        std::string line;
        while (std::getline(file, line)) {
            if (line.starts_with('[')) {
                inKey = std::ranges::any_of(kKeys, [&](const auto key) { return StartsWithIgnoreCase(line, key); });
            }
            else if (inKey && StartsWithIgnoreCase(line, kValuePrefix)) {
                auto value = DecodeWineRegistryString(std::string_view(line).substr(kValuePrefix.size()));
                if (!value.empty()) {
                    return value;
                }
            }
        }
        return std::nullopt;
    }

    // Maps a Windows path such as C:\Program Files\SimCity 4 to its location inside a Wine prefix.
    auto MapWindowsPathIntoPrefix(const fs::path& prefix, const std::string_view windowsPath) -> std::optional<fs::path> {
        if (windowsPath.size() < 2 || windowsPath[1] != ':' || !std::isalpha(static_cast<unsigned char>(windowsPath[0]))) {
            return std::nullopt;
        }

        const char drive = static_cast<char>(std::tolower(static_cast<unsigned char>(windowsPath[0])));
        fs::path mapped = prefix / "dosdevices" / std::string{drive, ':'};
        if (!IsDirectory(mapped)) {
            if (drive != 'c') {
                return std::nullopt;
            }
            mapped = prefix / "drive_c";
        }

        size_t start = 2;
        while (start < windowsPath.size()) {
            size_t end = windowsPath.find_first_of("\\/", start);
            if (end == std::string_view::npos) {
                end = windowsPath.size();
            }
            if (end > start) {
                mapped /= std::string(windowsPath.substr(start, end - start));
            }
            start = end + 1;
        }
        return ResolvePathCaseInsensitive(mapped);
    }

    auto FindGameRootInPrefix(const fs::path& prefix) -> std::optional<DetectedPath> {
        if (const auto installDir = ReadWineInstallDir(prefix)) {
            if (auto mapped = MapWindowsPathIntoPrefix(prefix, *installDir); mapped && IsDirectory(*mapped)) {
                return DetectedPath{std::move(*mapped), "Wine registry"};
            }
        }

        constexpr std::array<std::string_view, 7> kCommonInstallDirs{
            R"(C:\Program Files (x86)\SimCity 4 Deluxe Edition)",
            R"(C:\Program Files\SimCity 4 Deluxe Edition)",
            R"(C:\Program Files (x86)\Maxis\SimCity 4 Deluxe)",
            R"(C:\Program Files\Maxis\SimCity 4 Deluxe)",
            R"(C:\GOG Games\SimCity 4 Deluxe Edition)",
            R"(C:\Program Files (x86)\GOG Galaxy\Games\SimCity 4 Deluxe Edition)",
            R"(C:\Program Files (x86)\Steam\steamapps\common\SimCity 4 Deluxe)",
        };
        for (const auto installDir : kCommonInstallDirs) {
            if (auto mapped = MapWindowsPathIntoPrefix(prefix, installDir); mapped && IsGameRoot(*mapped)) {
                return DetectedPath{std::move(*mapped), "Wine prefix"};
            }
        }
        return std::nullopt;
    }

    // Finds Documents/SimCity 4 for the prefix's users, preferring the current user and Proton's steamuser.
    auto FindSc4DocumentsInPrefix(const fs::path& prefix) -> std::optional<fs::path> {
        const auto usersDir = prefix / "drive_c" / "users";
        if (!IsDirectory(usersDir)) {
            return std::nullopt;
        }

        std::vector<fs::path> userDirs;
        const auto addUser = [&](const fs::path& dir) {
            if (IsDirectory(dir) && std::ranges::find(userDirs, dir) == userDirs.end()) {
                userDirs.push_back(dir);
            }
        };
        if (const auto user = GetEnvironmentPath("USER")) {
            addUser(usersDir / *user);
        }
        addUser(usersDir / "steamuser");
        for (const auto& dir : SortedSubdirectories(usersDir)) {
            if (!EqualsIgnoreCase(dir.filename().string(), "Public")) {
                addUser(dir);
            }
        }

        for (const auto& userDir : userDirs) {
            for (const auto* documents : {"Documents", "My Documents"}) {
                auto sc4Documents = ResolvePathCaseInsensitive(userDir / documents / "SimCity 4");
                if (IsDirectory(sc4Documents)) {
                    return sc4Documents;
                }
            }
        }
        return std::nullopt;
    }

    // Steam library roots (steamapps parents), including extra libraries from libraryfolders.vdf.
    auto FindSteamLibraries() -> std::vector<fs::path> {
        std::vector<fs::path> steamRoots;
        const auto home = GetEnvironmentPath("HOME");
        if (const auto dataHome = GetEnvironmentPath("XDG_DATA_HOME")) {
            steamRoots.push_back(*dataHome / "Steam");
        }
        if (home) {
            steamRoots.push_back(*home / ".local" / "share" / "Steam");
            steamRoots.push_back(*home / ".steam" / "steam");
            steamRoots.push_back(*home / ".var" / "app" / "com.valvesoftware.Steam" / ".local" / "share" / "Steam");
        }

        std::vector<fs::path> libraries;
        const auto addLibrary = [&](const fs::path& dir) {
            std::error_code ec;
            if (!IsDirectory(dir / "steamapps")) {
                return;
            }
            auto canonical = fs::canonical(dir, ec);
            if (ec) {
                canonical = dir;
            }
            if (std::ranges::find(libraries, canonical) == libraries.end()) {
                libraries.push_back(std::move(canonical));
            }
        };

        for (const auto& steamRoot : steamRoots) {
            addLibrary(steamRoot);

            std::ifstream vdf(steamRoot / "steamapps" / "libraryfolders.vdf");
            std::string line;
            while (std::getline(vdf, line)) {
                // Entries look like: "path"		"/mnt/games/SteamLibrary"
                const auto keyPos = line.find("\"path\"");
                if (keyPos == std::string::npos) {
                    continue;
                }
                const auto open = line.find('"', keyPos + 6);
                const auto close = open == std::string::npos ? open : line.rfind('"');
                if (open != std::string::npos && close > open) {
                    addLibrary(fs::path(line.substr(open + 1, close - open - 1)));
                }
            }
        }
        return libraries;
    }
#endif
}

auto GetExecutableDirectory(const char* argv0) -> fs::path {
    std::error_code ec;
#ifdef _WIN32
    std::wstring buffer(MAX_PATH, L'\0');
    while (buffer.size() <= 32768) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            break;
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return fs::path(buffer).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__linux__)
    if (auto executable = fs::read_symlink("/proc/self/exe", ec); !ec) {
        return executable.parent_path();
    }
#endif
    if (argv0 && argv0[0] != '\0') {
        if (auto executable = fs::absolute(argv0, ec); !ec) {
            return executable.parent_path();
        }
    }
    return {};
}

auto ResolvePathCaseInsensitive(const fs::path& path) -> fs::path {
#ifdef _WIN32
    return path;
#else
    std::error_code ec;
    if (path.empty() || fs::exists(path, ec)) {
        return path;
    }

    fs::path resolved = path.root_path();
    for (const auto& component : path.relative_path()) {
        if (auto candidate = resolved / component; fs::exists(candidate, ec)) {
            resolved = std::move(candidate);
            continue;
        }

        std::optional<fs::path> match;
        const fs::path searchDir = resolved.empty() ? fs::path(".") : resolved;
        for (auto it = fs::directory_iterator(searchDir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            if (EqualsIgnoreCase(it->path().filename().native(), component.native())) {
                match = resolved / it->path().filename();
                break;
            }
        }
        if (!match) {
            return path;
        }
        resolved = std::move(*match);
    }
    return resolved;
#endif
}

auto DetectPluginPaths(const std::optional<fs::path>& winePrefix) -> DetectedPluginPaths {
    DetectedPluginPaths result;

#ifdef _WIN32
    (void)winePrefix;

    // Settings saved by the installer take precedence, then the game's own registry key.
    if (auto installerGameRoot = ReadRegistryString(HKEY_CURRENT_USER, L"Software\\SC4PlopAndPaint", L"GameRoot", 0);
        installerGameRoot && IsDirectory(*installerGameRoot)) {
        result.gameRoot = DetectedPath{std::move(*installerGameRoot), "installer settings"};
    }
    else if (auto installDir = ReadRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Maxis\\SimCity 4", L"Install Dir",
                                                  KEY_WOW64_32KEY);
             installDir && IsDirectory(*installDir)) {
        result.gameRoot = DetectedPath{std::move(*installDir), "Maxis registry key"};
    }
    else if (const auto programFiles = GetKnownFolder(FOLDERID_ProgramFilesX86);
             IsGameRoot(programFiles / kDefaultInstallFolder)) {
        result.gameRoot = DetectedPath{programFiles / kDefaultInstallFolder, "default install location"};
    }

    if (auto installerPlugins = ReadRegistryString(HKEY_CURRENT_USER, L"Software\\SC4PlopAndPaint", L"PluginsDir", 0)) {
        result.userPluginsRoot = DetectedPath{std::move(*installerPlugins), "installer settings"};
    }
    else if (const auto documents = GetKnownFolder(FOLDERID_Documents); !documents.empty()) {
        result.userPluginsRoot = DetectedPath{documents / "SimCity 4" / "Plugins", "Documents folder"};
    }
#else
    const auto steamLibraries = FindSteamLibraries();

    std::vector<std::pair<fs::path, std::string>> prefixes;
    if (winePrefix) {
        prefixes.emplace_back(*winePrefix, "--wine-prefix");
    }
    else if (const auto envPrefix = GetEnvironmentPath("WINEPREFIX")) {
        prefixes.emplace_back(*envPrefix, "WINEPREFIX");
    }
    else {
        if (const auto home = GetEnvironmentPath("HOME")) {
            prefixes.emplace_back(*home / ".wine", "default Wine prefix");
        }
        for (const auto& library : steamLibraries) {
            for (const auto& appData : SortedSubdirectories(library / "steamapps" / "compatdata")) {
                prefixes.emplace_back(appData / "pfx", "Steam Proton prefix");
            }
        }
    }

    for (const auto& [prefix, origin] : prefixes) {
        if (!IsDirectory(prefix / "drive_c")) {
            continue;
        }
        const auto documents = FindSc4DocumentsInPrefix(prefix);
        auto gameRoot = FindGameRootInPrefix(prefix);
        if (!documents && !gameRoot) {
            continue;
        }

        result.winePrefix = prefix;
        if (documents) {
            result.userPluginsRoot = DetectedPath{ResolvePathCaseInsensitive(*documents / "Plugins"), origin};
        }
        if (gameRoot) {
            gameRoot->source += " (" + origin + ")";
            result.gameRoot = std::move(gameRoot);
        }
        break;
    }

    // Steam keeps the game itself in the library, outside the Proton prefix.
    if (!result.gameRoot) {
        for (const auto& library : steamLibraries) {
            for (const auto& dir : SortedSubdirectories(library / "steamapps" / "common")) {
                if (StartsWithIgnoreCase(dir.filename().string(), "SimCity 4") && IsGameRoot(dir)) {
                    result.gameRoot = DetectedPath{dir, "Steam library"};
                    break;
                }
            }
            if (result.gameRoot) {
                break;
            }
        }
    }
#endif

    return result;
}
