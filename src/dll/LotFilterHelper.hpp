#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include "entities.hpp"

// Lot size constraints (shared with UI)
namespace LotSize {
    constexpr int kMinSize = 1;
    constexpr int kMaxSize = 16;
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

    /**
     * Check if a lot passes all active filters.
     * @param lot The lot to check
     * @return true if the lot passes all filters
     */
    bool PassesFilters(const Lot& lot) const;

    /**
     * Apply filters and sort lots with favorites appearing first.
     * @param lots Vector of all lots to filter
     * @param favorites Set of favorited lot instance IDs
     * @return Filtered and sorted vector of lot pointers
     */
    std::vector<const Lot*> ApplyFiltersAndSort(
        const std::vector<Lot>& lots,
        const std::unordered_set<uint32_t>& favorites
    ) const;

    /**
     * Reset all filters to their default values.
     */
    void ResetFilters();

private:
    /**
     * Check if lot passes text search filter (lot name or building name).
     * Case-insensitive search.
     */
    bool PassesTextFilter_(const Lot& lot) const;

    /**
     * Check if lot passes size filter (within min/max X and Z bounds).
     */
    bool PassesSizeFilter_(const Lot& lot) const;

    /**
     * Check if lot passes occupant group filter.
     * Returns true if no OGs selected OR if ANY lot OG matches ANY selected OG.
     */
    bool PassesOccupantGroupFilter_(const Lot& lot) const;
};
