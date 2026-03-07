#pragma once
#include <filesystem>
#include <unordered_map>
#include <vector>

#include "../../shared/entities.hpp"
#include "../thumbnail/ThumbnailStore.hpp"

class LotRepository {
public:
    void Load();

    [[nodiscard]] const std::vector<Building>& GetBuildings() const { return buildings_; }
    [[nodiscard]] const std::unordered_map<uint64_t, const Building*>& GetBuildingsById() const { return buildingsById_; }
    [[nodiscard]] ThumbnailStore& GetBuildingThumbnailStore() { return buildingThumbnails_; }

private:
    static std::filesystem::path GetPluginsPath_();

    std::vector<Building> buildings_;
    std::unordered_map<uint64_t, const Building*> buildingsById_;
    ThumbnailStore buildingThumbnails_;
};
