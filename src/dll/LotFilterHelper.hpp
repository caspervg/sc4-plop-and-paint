#pragma once
#include <format>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>
#include "entities.hpp"

// Lot size constraints
namespace LotSize {
    constexpr auto kMinSize = 1;
    constexpr auto kMaxSize = 64;
}

struct LotView {
    const Building* building = nullptr;
    const Lot* lot = nullptr;

    [[nodiscard]] std::string BuildingNodeID() const {
        return std::format("B{}", building->instanceId.value());
    }

    [[nodiscard]] std::string BuildingLotNodeID() const {
        return std::format("B{}L{}", building->instanceId.value(), lot->instanceId.value());
    }
};

/**
 * Helper class for filtering and sorting lots in the panel.
 * Encapsulates all filter state and provides reusable filtering logic.
 */
class LotFilterHelper {
public:
    enum class SortColumn {
        Name,   // Building name, then lot name
        Size    // Area, then width, then depth
    };

    struct SortSpec {
        SortColumn column = SortColumn::Name;
        bool descending = false;
    };

    // Filter state
    std::string searchBuffer;
    int minSizeX = LotSize::kMinSize;
    int minSizeZ = LotSize::kMinSize;
    int maxSizeX = LotSize::kMaxSize;
    int maxSizeZ = LotSize::kMaxSize;
    std::unordered_set<uint32_t> selectedOccupantGroups;
    std::optional<uint8_t> selectedZoneType; // None = show all, otherwise filter by zone type
    std::optional<uint8_t> selectedWealthType; // None = show all, otherwise filter by wealth
    std::optional<uint8_t> selectedGrowthStage; // None = show all, 0-15 or 255 for plopped
    bool favoritesOnly = false; // If true, only show favorited lots

    [[nodiscard]] bool PassesFilters(const LotView& lot) const;

    [[nodiscard]] std::vector<LotView> ApplyFiltersAndSort(
        const std::vector<LotView>& lots,
        const std::unordered_set<uint32_t>& favorites,
        std::span<const SortSpec> sortOrder
    ) const;

    void ResetFilters();

private:
    [[nodiscard]] bool PassesTextFilter_(const LotView& lot) const;
    [[nodiscard]] bool PassesSizeFilter_(const LotView& lot) const;
    [[nodiscard]] bool PassesOccupantGroupFilter_(const LotView& lot) const;
    [[nodiscard]] bool PassesZoneTypeFilter_(const LotView& lot) const;
    [[nodiscard]] bool PassesWealthFilter_(const LotView& lot) const;
    [[nodiscard]] bool PassesGrowthStageFilter_(const LotView& lot) const;
    [[nodiscard]] bool PassesFavoritesOnlyFilter_(const LotView& lot, const std::unordered_set<uint32_t>& favorites) const;
};
