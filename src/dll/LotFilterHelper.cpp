#include "LotFilterHelper.hpp"
#include <algorithm>
#include <cctype>

#include "spdlog/spdlog.h"

bool LotFilterHelper::PassesFilters(const LotView& lot) const {
    return PassesTextFilter_(lot) &&
           PassesSizeFilter_(lot) &&
           PassesOccupantGroupFilter_(lot) &&
           PassesZoneTypeFilter_(lot) &&
           PassesWealthFilter_(lot) &&
           PassesGrowthStageFilter_(lot);
}

std::vector<LotView> LotFilterHelper::ApplyFiltersAndSort(
    const std::vector<LotView>& lots,
    const std::unordered_set<uint32_t>& favorites,
    std::span<const SortSpec> sortOrder
) const {
    std::vector<LotView> filtered;

    // Filter lots
    for (const auto& lot : lots) {
        if (PassesFilters(lot) && PassesFavoritesOnlyFilter_(lot, favorites)) {
            filtered.push_back(lot);
        }
    }

    const auto compareStrings = [](const std::string& lhs, const std::string& rhs) {
        if (lhs < rhs) return -1;
        if (lhs > rhs) return 1;
        return 0;
    };

    const auto compareInts = [](auto lhs, auto rhs) {
        if (lhs < rhs) return -1;
        if (lhs > rhs) return 1;
        return 0;
    };

    // Effective sort order: UI-provided specs or default name ordering.
    std::vector<SortSpec> effectiveOrder;
    if (!sortOrder.empty()) {
        effectiveOrder.assign(sortOrder.begin(), sortOrder.end());
    }
    else {
        effectiveOrder.push_back({SortColumn::Name, false});
    }

    // Always add a deterministic tie-breaker.
    effectiveOrder.push_back({SortColumn::Name, false});

    std::ranges::sort(filtered, [&](const LotView& a, const LotView& b) {
        for (const auto& spec : effectiveOrder) {
            int cmp = 0;
            switch (spec.column) {
                case SortColumn::Name: {
                    cmp = compareStrings(a.building->name, b.building->name);
                    if (cmp == 0) {
                        cmp = compareStrings(a.lot->name, b.lot->name);
                    }
                    break;
                }
                case SortColumn::Size: {
                    const auto areaA = static_cast<int>(a.lot->sizeX) * static_cast<int>(a.lot->sizeZ);
                    const auto areaB = static_cast<int>(b.lot->sizeX) * static_cast<int>(b.lot->sizeZ);
                    cmp = compareInts(areaA, areaB);
                    if (cmp == 0) cmp = compareInts(a.lot->sizeX, b.lot->sizeX);
                    if (cmp == 0) cmp = compareInts(a.lot->sizeZ, b.lot->sizeZ);
                    break;
                }
            }

            if (cmp != 0) {
                return spec.descending ? (cmp > 0) : (cmp < 0);
            }
        }

        // Final stable tie-breaker on IDs to keep order deterministic.
        const auto buildingCmp = compareInts(a.building->instanceId.value(), b.building->instanceId.value());
        if (buildingCmp != 0) return buildingCmp < 0;
        return a.lot->instanceId.value() < b.lot->instanceId.value();
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

bool LotFilterHelper::PassesTextFilter_(const LotView& view) const {
    // If search buffer is empty, all lots pass
    if (searchBuffer.empty()) {
        return true;
    }
    const Lot& lot = *view.lot;
    const Building& building = *view.building;

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
    buildingNameLower.reserve(building.name.size());
    std::ranges::transform(building.name, std::back_inserter(buildingNameLower),
                           [](unsigned char c) { return std::tolower(c); });
    if (buildingNameLower.find(searchLower) != std::string::npos) {
        return true;
    }

    return false;
}

bool LotFilterHelper::PassesSizeFilter_(const LotView& view) const {
    const Lot& lot = *view.lot;
    int effectiveMinX = std::min(minSizeX, maxSizeX);
    int effectiveMaxX = std::max(minSizeX, maxSizeX);
    int effectiveMinZ = std::min(minSizeZ, maxSizeZ);
    int effectiveMaxZ = std::max(minSizeZ, maxSizeZ);

    return lot.sizeX >= effectiveMinX && lot.sizeX <= effectiveMaxX &&
        lot.sizeZ >= effectiveMinZ && lot.sizeZ <= effectiveMaxZ;
}

bool LotFilterHelper::PassesOccupantGroupFilter_(const LotView& view) const {
    const Building& building = *view.building;
    // If no occupant groups are selected, show all lots
    if (selectedOccupantGroups.empty()) {
        return true;
    }

    // Check if any of the lot's occupant groups matches any selected OG
    return std::ranges::any_of(building.occupantGroups,
                               [this](uint32_t og) { return selectedOccupantGroups.contains(og); });
}

bool LotFilterHelper::PassesZoneTypeFilter_(const LotView& view) const {
    const Lot& lot = *view.lot;
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

bool LotFilterHelper::PassesWealthFilter_(const LotView& view) const {
    const Lot& lot = *view.lot;
    // If no wealth type is selected, show all lots
    if (!selectedWealthType.has_value()) {
        return true;
    }

    // Check if lot has a wealth type and it matches the selected wealth type
    return lot.wealthType.has_value() && lot.wealthType.value() == selectedWealthType.value();
}

bool LotFilterHelper::PassesGrowthStageFilter_(const LotView& view) const {
    const Lot& lot = *view.lot;
    // If no growth stage is selected, show all lots
    if (!selectedGrowthStage.has_value()) {
        return true;
    }

    // Check if lot's growth stage matches the selected growth stage
    return lot.growthStage == selectedGrowthStage.value();
}

bool LotFilterHelper::PassesFavoritesOnlyFilter_(const LotView& view, const std::unordered_set<uint32_t>& favorites) const {
    // If favorites only is not enabled, show all lots
    if (!favoritesOnly) {
        return true;
    }

    // Check if lot is in the favorites set
    return favorites.contains(view.lot->instanceId.value());
}
