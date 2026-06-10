#include "PluginLocator.hpp"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <iterator>
#include <vector>

namespace fs = std::filesystem;

namespace {
    using PathString = fs::path::string_type;

    auto ToUpperPathString(const PathString& value) -> PathString {
        PathString upper = value;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](auto c) {
            using Char = std::decay_t<decltype(c)>;
            if constexpr (sizeof(Char) == 1) {
                return static_cast<Char>(std::toupper(static_cast<unsigned char>(c)));
            }
            else {
                return static_cast<Char>(std::towupper(static_cast<std::wint_t>(c)));
            }
        });
        return upper;
    }

    auto IsDatFile(const fs::path& file) -> bool {
        auto ext = file.extension().string();
        std::ranges::transform(ext, ext.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return ext == ".dat";
    }

    struct LoadOrderEntry {
        fs::path path;
        bool isDat = false;
        std::vector<PathString> components; // path components relative to the tree root, uppercased
    };

    // Game load order within a single plugin tree (matches jDBPFX's SimCityFileComparator):
    // 1. .dat files load after all other DBPF files in the tree.
    // 2. Files in a folder load before any of its subfolders.
    // 3. Otherwise case-insensitive (uppercase) path order, matching NTFS enumeration,
    //    where '_' sorts after alphanumeric characters.
    auto GameLoadOrderLess(const LoadOrderEntry& a, const LoadOrderEntry& b) -> bool {
        if (a.isDat != b.isDat) {
            return !a.isDat;
        }

        const size_t depth = std::min(a.components.size(), b.components.size());
        for (size_t i = 0; i < depth; ++i) {
            const bool aIsFile = i + 1 == a.components.size();
            const bool bIsFile = i + 1 == b.components.size();
            if (aIsFile != bIsFile) {
                return aIsFile;
            }
            if (a.components[i] != b.components[i]) {
                return a.components[i] < b.components[i];
            }
        }
        return a.components.size() < b.components.size();
    }

    auto SortByGameLoadOrder(const fs::path& root, std::vector<fs::path>& files) -> void {
        std::vector<LoadOrderEntry> entries;
        entries.reserve(files.size());
        for (auto& file : files) {
            LoadOrderEntry entry;
            entry.isDat = IsDatFile(file);
            for (const auto& component : file.lexically_relative(root)) {
                entry.components.push_back(ToUpperPathString(component.native()));
            }
            entry.path = std::move(file);
            entries.push_back(std::move(entry));
        }

        std::stable_sort(entries.begin(), entries.end(), GameLoadOrderLess);

        files.clear();
        for (auto& entry : entries) {
            files.push_back(std::move(entry.path));
        }
    }
}

PluginLocator::PluginLocator(PluginConfiguration config) : config_(std::move(config)) {}

auto PluginLocator::ListDbpfFiles() const -> std::vector<std::filesystem::path> {
    std::vector<std::filesystem::path> files;
    CollectTreeSorted_(config_.gameRoot, false, files);
    if (!config_.localeDir.empty()) {
        CollectTreeSorted_(config_.gameRoot / config_.localeDir, false, files);
    }
    CollectTreeSorted_(config_.gamePluginsRoot, true, files);
    CollectTreeSorted_(config_.userPluginsRoot, true, files);

    return files;
}

auto PluginLocator::CollectTreeSorted_(const std::filesystem::path& root, bool recursive,
                                       std::vector<std::filesystem::path>& out) -> void {
    std::vector<std::filesystem::path> tree;
    CollectFiles_(root, recursive, tree);
    SortByGameLoadOrder(root, tree);
    out.insert(out.end(), std::make_move_iterator(tree.begin()), std::make_move_iterator(tree.end()));
}

auto PluginLocator::CollectFiles_(const std::filesystem::path& root, bool recursive,
                                  std::vector<std::filesystem::path>& out) -> void {
    if (root.empty())
        return;
    std::error_code ec;

    if (!std::filesystem::exists(root, ec))
        return;

    if (recursive) {
        FindPlugins(std::filesystem::recursive_directory_iterator(root, kDirectoryOptions, ec),
             std::filesystem::recursive_directory_iterator(), out);
    }
    else {
        FindPlugins(std::filesystem::directory_iterator(root, ec), std::filesystem::directory_iterator(), out);
    }
}