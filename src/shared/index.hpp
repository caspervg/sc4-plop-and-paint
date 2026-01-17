#pragma once

#include <filesystem>

#include "rfl/Timestamp.hpp"

constexpr auto kRFC3339TimeFormat = "%FT%TZ";

struct PluginFileInfo {
    std::string filePath;
    uint64_t fileSize;
    rfl::Timestamp<kRFC3339TimeFormat> lastModified;
    uint32_t resourceCount;
};

struct PluginIndex {
    rfl::Timestamp<kRFC3339TimeFormat> buildTime;
    std::string pluginsDirectory;
    std::vector<PluginFileInfo> files;
};

struct PluginConfiguration {
    std::filesystem::path gameRoot;
    std::filesystem::path gamePluginsRoot;
    std::filesystem::path userPluginsRoot;
};