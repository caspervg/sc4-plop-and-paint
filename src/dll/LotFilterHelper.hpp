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
};
