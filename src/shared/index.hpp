#pragma once

#include <filesystem>

#include "rfl/Timestamp.hpp"

struct PluginFileInfo {
    std::string filePath;
    uint64_t fileSize;
    rfl::Timestamp<"%FT%TZ"> lastModified;
    uint32_t resourceCount;
};

struct PluginIndex {
    rfl::Timestamp<"%FT%TZ"> buildTime;
    std::string pluginsDirectory;
    std::vector<PluginFileInfo> files;
};

struct PluginConfiguration {
    std::filesystem::path gameRoot;
    std::filesystem::path localeDir;
    std::filesystem::path gamePluginsRoot;
    std::filesystem::path userPluginsRoot;
};
