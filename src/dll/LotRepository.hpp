#pragma once
#include <filesystem>
#include <unordered_map>
#include <vector>

#include "../shared/entities.hpp"

class LotRepository {
public:
    void Load();

    [[nodiscard]] const std::vector<Building>& GetBuildings() const { return buildings_; }
    [[nodiscard]] const std::unordered_map<uint64_t, Building>& GetBuildingsById() const { return buildingsById_; }
    [[nodiscard]] const std::unordered_map<uint64_t, Lot>& GetLotsById() const { return lotsById_; }

private:
    static std::filesystem::path GetPluginsPath_();

    std::vector<Building> buildings_;
    std::unordered_map<uint64_t, Building> buildingsById_;
    std::unordered_map<uint64_t, Lot> lotsById_;
};
