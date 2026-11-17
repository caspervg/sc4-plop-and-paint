#pragma once
#include <unordered_set>

#include "index.hpp"

const auto kDbpfFileExtensions = std::unordered_set<std::string>{
    ".dat", ".sc4lot", ".sc4model", ".sc4desc"
};

class PluginLocator {
public:
    explicit PluginLocator(PluginConfiguration config);

    [[nodiscard]] auto ListDbpfFiles() const -> std::vector<std::filesystem::path>;

private:
    static auto CollectTreeSorted_(const std::filesystem::path& root, bool recursive, std::vector<std::filesystem::path>& out) -> void;
    static auto CollectFiles_(const std::filesystem::path& root, bool recursive, std::vector<std::filesystem::path>& out) -> void;

private:
    PluginConfiguration config_;
};
