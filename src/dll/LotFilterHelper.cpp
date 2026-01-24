#include "LotFilterHelper.hpp"
#include <algorithm>
#include <cctype>

bool LotFilterHelper::PassesFilters(const Lot& lot) const {
    return PassesTextFilter_(lot) &&
           PassesSizeFilter_(lot) &&
           PassesOccupantGroupFilter_(lot) &&
           PassesZoneTypeFilter_(lot) &&
           PassesWealthFilter_(lot) &&
           PassesGrowthStageFilter_(lot);
}

std::vector<const Lot*> LotFilterHelper::ApplyFiltersAndSort(
    const std::vector<Lot>& lots,
    const std::unordered_set<uint32_t>& favorites
) const {
    std::vector<const Lot*> filtered;

    // Filter lots
    for (const auto& lot : lots) {
        if (PassesFilters(lot) && PassesFavoritesOnlyFilter_(lot, favorites)) {
            filtered.push_back(&lot);
        }
    }

    // Sort with two-tier comparator: favorites first, then alphabetically
    std::ranges::sort(filtered, [&favorites](const Lot* a, const Lot* b) {
        const auto aIsFavorite = favorites.contains(a->instanceId.value());
        const auto bIsFavorite = favorites.contains(b->instanceId.value());

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
    searchBuffer.clear();

    // Reset sliders to full range
    minSizeX = LotSize::kMinSize;
    minSizeZ = LotSize::kMinSize;
    maxSizeX = LotSize::kMaxSize;
    maxSizeZ = LotSize::kMaxSize;

    selectedOccupantGroups.clear();
    selectedZoneType.reset();
    selectedWealthType.reset();
    selectedGrowthStage.reset();
    favoritesOnly = false;
}

bool LotFilterHelper::PassesTextFilter_(const Lot& lot) const {
    // If search buffer is empty, all lots pass
    if (searchBuffer.empty()) {
        return true;
    }

    // Convert search term to lowercase for case-insensitive search (using std::transform)
    std::string searchLower;
    searchLower.reserve(searchBuffer.size());
    std::ranges::transform(searchBuffer, std::back_inserter(searchLower),
                           [](unsigned char c) { return std::tolower(c); });

    // Convert lot name to lowercase and search
    std::string lotNameLower;
    lotNameLower.reserve(lot.name.size());
    std::ranges::transform(lot.name, std::back_inserter(lotNameLower),
                           [](unsigned char c) { return std::tolower(c); });
    if (lotNameLower.find(searchLower) != std::string::npos) {
        return true;
    }

    // Convert building name to lowercase and search
    std::string buildingNameLower;
    buildingNameLower.reserve(lot.building.name.size());
    std::ranges::transform(lot.building.name, std::back_inserter(buildingNameLower),
                           [](unsigned char c) { return std::tolower(c); });
    if (buildingNameLower.find(searchLower) != std::string::npos) {
        return true;
    }

    return false;
}

bool LotFilterHelper::PassesSizeFilter_(const Lot& lot) const {
    int effectiveMinX = std::min(minSizeX, maxSizeX);
    int effectiveMaxX = std::max(minSizeX, maxSizeX);
    int effectiveMinZ = std::min(minSizeZ, maxSizeZ);
    int effectiveMaxZ = std::max(minSizeZ, maxSizeZ);

    return lot.sizeX >= effectiveMinX && lot.sizeX <= effectiveMaxX &&
        lot.sizeZ >= effectiveMinZ && lot.sizeZ <= effectiveMaxZ;
}

bool LotFilterHelper::PassesOccupantGroupFilter_(const Lot& lot) const {
    // If no occupant groups are selected, show all lots
    if (selectedOccupantGroups.empty()) {
        return true;
    }

    // Check if any of the lot's occupant groups matches any selected OG
    return std::ranges::any_of(lot.building.occupantGroups,
                               [this](uint32_t og) { return selectedOccupantGroups.contains(og); });
}

bool LotFilterHelper::PassesZoneTypeFilter_(const Lot& lot) const {
    // If no zone type is selected, show all lots
    if (!selectedZoneType.has_value()) {
        return true;
    }

    // If lot has no zone type, it only passes if "None" is selected (category 4)
    if (!lot.zoneType.has_value()) {
        return selectedZoneType.value() == 4;  // "None" category
    }

    const uint8_t zoneValue = lot.zoneType.value();
    const uint8_t category = selectedZoneType.value();

    // Map zone type to category:
    // 0 = Residential (R) - matches 0x01-0x03
    // 1 = Commercial (C) - matches 0x04-0x06
    // 2 = Industrial (I) - matches 0x07-0x09
    // 3 = Plopped - matches 0x0F
    // 4 = None - matches 0x00
    // 5 = Other - matches 0x0A-0x0E (Military, Airport, Seaport, Spaceport, Landfill)

    if (category == 0) {  // Residential
        return zoneValue >= 0x01 && zoneValue <= 0x03;
    } else if (category == 1) {  // Commercial
        return zoneValue >= 0x04 && zoneValue <= 0x06;
    } else if (category == 2) {  // Industrial
        return zoneValue >= 0x07 && zoneValue <= 0x09;
    } else if (category == 3) {  // Plopped
        return zoneValue == 0x0F;
    } else if (category == 4) {  // None
        return zoneValue == 0x00;
    } else if (category == 5) {  // Other
        return zoneValue >= 0x0A && zoneValue <= 0x0E;
    }

    return false;
}

bool LotFilterHelper::PassesWealthFilter_(const Lot& lot) const {
    // If no wealth type is selected, show all lots
    if (!selectedWealthType.has_value()) {
        return true;
    }

    // Check if lot has a wealth type and it matches the selected wealth type
    return lot.wealthType.has_value() && lot.wealthType.value() == selectedWealthType.value();
}

bool LotFilterHelper::PassesGrowthStageFilter_(const Lot& lot) const {
    // If no growth stage is selected, show all lots
    if (!selectedGrowthStage.has_value()) {
        return true;
    }

    // Check if lot's growth stage matches the selected growth stage
    return lot.growthStage == selectedGrowthStage.value();
}

bool LotFilterHelper::PassesFavoritesOnlyFilter_(const Lot& lot, const std::unordered_set<uint32_t>& favorites) const {
    // If favorites only is not enabled, show all lots
    if (!favoritesOnly) {
        return true;
    }

    // Check if lot is in the favorites set
    return favorites.contains(lot.instanceId.value());
}
