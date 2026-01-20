#include "LotFilterHelper.hpp"
#include <algorithm>
#include <cctype>

bool LotFilterHelper::PassesTextFilter_(const Lot& lot) const {
    // If search buffer is empty, all lots pass
    if (searchBuffer.empty()) {
        return true;
    }

    // Convert search term to lowercase for case-insensitive search (using std::transform)
    std::string searchLower;
    searchLower.reserve(searchBuffer.size());
    std::transform(searchBuffer.begin(), searchBuffer.end(), std::back_inserter(searchLower),
                   [](unsigned char c) { return std::tolower(c); });

    // Convert lot name to lowercase and search
    std::string lotNameLower;
    lotNameLower.reserve(lot.name.size());
    std::transform(lot.name.begin(), lot.name.end(), std::back_inserter(lotNameLower),
                   [](unsigned char c) { return std::tolower(c); });
    if (lotNameLower.find(searchLower) != std::string::npos) {
        return true;
    }

    // Convert building name to lowercase and search
    std::string buildingNameLower;
    buildingNameLower.reserve(lot.building.name.size());
    std::transform(lot.building.name.begin(), lot.building.name.end(), std::back_inserter(buildingNameLower),
                   [](unsigned char c) { return std::tolower(c); });
    if (buildingNameLower.find(searchLower) != std::string::npos) {
        return true;
    }

    return false;
}

bool LotFilterHelper::PassesSizeFilter_(const Lot& lot) const {
    // Auto-clamp: ensure min <= max
    int effectiveMinX = std::min(minSizeX, maxSizeX);
    int effectiveMaxX = std::max(minSizeX, maxSizeX);
    int effectiveMinZ = std::min(minSizeZ, maxSizeZ);
    int effectiveMaxZ = std::max(minSizeZ, maxSizeZ);

    // Check if lot size is within bounds
    return lot.sizeX >= effectiveMinX && lot.sizeX <= effectiveMaxX &&
           lot.sizeZ >= effectiveMinZ && lot.sizeZ <= effectiveMaxZ;
}

bool LotFilterHelper::PassesOccupantGroupFilter_(const Lot& lot) const {
    // If no occupant groups are selected, show all lots
    if (selectedOccupantGroups.empty()) {
        return true;
    }

    // Check if ANY of the lot's occupant groups matches ANY selected OG (using std::any_of)
    return std::any_of(lot.building.occupantGroups.begin(), lot.building.occupantGroups.end(),
        [this](uint32_t og) { return selectedOccupantGroups.contains(og); });
}

bool LotFilterHelper::PassesFilters(const Lot& lot) const {
    // Lot must pass ALL filters (AND logic)
    return PassesTextFilter_(lot) &&
           PassesSizeFilter_(lot) &&
           PassesOccupantGroupFilter_(lot);
}

std::vector<const Lot*> LotFilterHelper::ApplyFiltersAndSort(
    const std::vector<Lot>& lots,
    const std::unordered_set<uint32_t>& favorites
) const {
    std::vector<const Lot*> filtered;

    // Filter lots
    for (const auto& lot : lots) {
        if (PassesFilters(lot)) {
            filtered.push_back(&lot);
        }
    }

    // Sort with two-tier comparator: favorites first, then alphabetically
    std::sort(filtered.begin(), filtered.end(), [&favorites](const Lot* a, const Lot* b) {
        bool aIsFavorite = favorites.contains(a->instanceId.value());
        bool bIsFavorite = favorites.contains(b->instanceId.value());

        // If one is favorite and the other isn't, favorite comes first
        if (aIsFavorite != bIsFavorite) {
            return aIsFavorite;
        }

        // Both favorites or both non-favorites: sort alphabetically by name
        return a->name < b->name;
    });

    return filtered;
}

void LotFilterHelper::ResetFilters() {
    // Clear search buffer
    searchBuffer.clear();

    // Reset size sliders to full range
    minSizeX = LotSize::kMinSize;
    minSizeZ = LotSize::kMinSize;
    maxSizeX = LotSize::kMaxSize;
    maxSizeZ = LotSize::kMaxSize;

    // Clear occupant group selection
    selectedOccupantGroups.clear();
}