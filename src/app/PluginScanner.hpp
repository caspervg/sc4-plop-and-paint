#pragma once
#include <filesystem>
#include <vector>


class PluginScanner {
public:
    std::vector<std::filesystem::path> scanDirectory(const std::filesystem::path& root);
private:
    bool isPluginFile_(const std::filesystem::path& filePath);
};
