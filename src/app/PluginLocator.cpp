#include "PluginLocator.hpp"

PluginLocator::PluginLocator(PluginConfiguration config) : config_(std::move(config)) {}

auto PluginLocator::ListDbpfFiles() const -> std::vector<std::filesystem::path> {
    std::vector<std::filesystem::path> files;
    CollectFiles_(config_.gameRoot, false, files);
    if (!config_.localeDir.empty()) {
        CollectFiles_(config_.gameRoot / config_.localeDir, false, files);
    }
    CollectFiles_(config_.gamePluginsRoot, true, files);
    CollectFiles_(config_.userPluginsRoot, true, files);

    return files;
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
