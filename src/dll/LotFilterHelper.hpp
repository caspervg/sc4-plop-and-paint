#pragma once
#include <string>
#include <unordered_set>
#include <vector>
#include "entities.hpp"

// Lot size constraints
namespace LotSize {
    constexpr auto kMinSize = 1;
    constexpr auto kMaxSize = 16;
}

/**
 * Helper class for filtering and sorting lots in the panel.
 * Encapsulates all filter state and provides reusable filtering logic.
 */
class LotFilterHelper {
public:
    // Filter state
    std::string searchBuffer;
    int minSizeX = LotSize::kMinSize;
    int minSizeZ = LotSize::kMinSize;
    int maxSizeX = LotSize::kMaxSize;
    int maxSizeZ = LotSize::kMaxSize;
    std::unordered_set<uint32_t> selectedOccupantGroups;
    std::optional<uint8_t> selectedZoneType;      // None = show all, otherwise filter by zone type
    std::optional<uint8_t> selectedWealthType;    // None = show all, otherwise filter by wealth
    std::optional<uint8_t> selectedGrowthStage;   // None = show all, 0-15 or 255 for plopped
    bool favoritesOnly = false;                   // If true, only show favorited lots

    [[nodiscard]] bool PassesFilters(const Lot& lot) const;

    [[nodiscard]] std::vector<const Lot*> ApplyFiltersAndSort(
        const std::vector<Lot>& lots,
        const std::unordered_set<uint32_t>& favorites
    ) const;

    void ResetFilters();

private:
    [[nodiscard]] bool PassesTextFilter_(const Lot& lot) const;
    [[nodiscard]] bool PassesSizeFilter_(const Lot& lot) const;
    [[nodiscard]] bool PassesOccupantGroupFilter_(const Lot& lot) const;
    [[nodiscard]] bool PassesZoneTypeFilter_(const Lot& lot) const;
    [[nodiscard]] bool PassesWealthFilter_(const Lot& lot) const;
    [[nodiscard]] bool PassesGrowthStageFilter_(const Lot& lot) const;
    [[nodiscard]] bool PassesFavoritesOnlyFilter_(const Lot& lot, const std::unordered_set<uint32_t>& favorites) const;
};
