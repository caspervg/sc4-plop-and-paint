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

    [[nodiscard]] const Building* FindBuildingByLotInstanceId(const uint32_t lotInstanceId) const {
        const auto it = buildingsByLotId_.find(lotInstanceId);
        return it != buildingsByLotId_.end() ? it->second : nullptr;
    }

private:
    static std::filesystem::path GetPluginsPath_();

    std::vector<Building> buildings_;
    std::unordered_map<uint64_t, const Building*> buildingsById_;
    std::unordered_map<uint32_t, const Building*> buildingsByLotId_;
    ThumbnailStore buildingThumbnails_;
};
