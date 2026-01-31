#pragma once
#include <unordered_set>

#include "index.hpp"

constexpr auto kDirectoryOptions = std::filesystem::directory_options::skip_permission_denied;
const auto kDbpfFileExtensions = std::unordered_set<std::string>{
    ".dat", ".sc4lot", ".sc4model", ".sc4desc"
};

template<typename Iter>
auto FindPlugins(Iter begin, Iter end, std::vector<std::filesystem::path>& out) -> void {
    for (auto it = begin; it != end; ++it) {
        if (! it->is_regular_file()) continue;
        auto ext = it->path().extension().string();
        std::ranges::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (kDbpfFileExtensions.contains(ext)) {
            out.push_back(it->path());
        }
    }
}

class PluginLocator {
public:
    explicit PluginLocator(PluginConfiguration config);

    [[nodiscard]] auto ListDbpfFiles() const -> std::vector<std::filesystem::path>;

private:
    static auto CollectFiles_(const std::filesystem::path& root, bool recursive, std::vector<std::filesystem::path>& out) -> void;

private:
    PluginConfiguration config_;
};
