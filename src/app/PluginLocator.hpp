#pragma once
#include <string>
#include <unordered_set>
#include <vector>

#include "index.hpp"

const auto kDbpfFileExtensions = std::unordered_set<std::string>{
    ".dat", ".sc4lot", ".sc4model", ".sc4desc"
};

// What the locator saw while walking the plugin folders, for the scanned files report.
struct PluginScanReport {
    struct Root {
        std::string label;
        std::filesystem::path path;
        bool recursive = false;
        size_t firstFile = 0; // index of the root's first file in the list of DBPF files
        size_t fileCount = 0;
    };
    struct SkippedFile {
        size_t root = 0;
        std::filesystem::path path;
    };
    struct UnreadableFolder {
        std::filesystem::path path;
        std::string error;
    };

    std::vector<Root> roots;
    std::vector<SkippedFile> skippedFiles; // files in a plugin folder that are not DBPF files
    std::vector<UnreadableFolder> unreadableFolders;
};

class PluginLocator {
public:
    explicit PluginLocator(PluginConfiguration config);

    // Lists the DBPF files in game load order. Fills report, when given, with what the walk saw.
    [[nodiscard]] auto ListDbpfFiles(PluginScanReport* report = nullptr) const -> std::vector<std::filesystem::path>;

private:
    static auto CollectTreeSorted_(std::string label, const std::filesystem::path& root, bool recursive,
                                   std::vector<std::filesystem::path>& out, PluginScanReport* report) -> void;
    static auto CollectFiles_(const std::filesystem::path& root, bool recursive, std::vector<std::filesystem::path>& out,
                              PluginScanReport* report) -> void;

private:
    PluginConfiguration config_;
};
