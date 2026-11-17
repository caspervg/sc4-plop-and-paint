#pragma once

#include <filesystem>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "DbpfIndexService.hpp"
#include "PluginLocator.hpp"

// Items from one file that made it into the cache, counting only the copy that wins in load order.
struct FileItemCounts {
    size_t buildings = 0;
    size_t lots = 0;
    size_t props = 0;
    size_t flora = 0;
    size_t unreadableRecords = 0;
};

// Writes a UTF-8 text file listing every file in the plugin folders as a tree, with whether and how it was
// read, so users can check why a plugin is or is not in the cache.
bool WriteScanReport(const std::filesystem::path& outputPath,
                     std::string_view version,
                     const std::vector<std::filesystem::path>& files,
                     const std::vector<IndexedFileResult>& results,
                     const PluginScanReport& report,
                     const std::unordered_map<std::filesystem::path, FileItemCounts>& itemCounts);
