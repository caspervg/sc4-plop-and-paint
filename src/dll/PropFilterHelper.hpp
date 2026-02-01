#pragma once
#include <span>
#include <string>
#include <unordered_set>
#include <vector>
#include "entities.hpp"

namespace PropSize {
    constexpr auto kMinSize = 0.0f;
    constexpr auto kMaxSize = 256.0f;
}

struct PropView {
    const Prop* prop = nullptr;
};

class PropFilterHelper {
public:
    enum class SortColumn {
        Name,
        Size
    };

    struct SortSpec {
        SortColumn column = SortColumn::Name;
        bool descending = false;
    };

    std::string searchBuffer;
    float propWidth[2] = {PropSize::kMinSize, PropSize::kMaxSize};
    float propHeight[2] = {PropSize::kMinSize, PropSize::kMaxSize};
    float propDepth[2] = {PropSize::kMinSize, PropSize::kMaxSize};
    bool favoritesOnly = false;

    [[nodiscard]] bool PassesFilters(const PropView& view) const;
    [[nodiscard]] std::vector<PropView> ApplyFiltersAndSort(
        const std::vector<PropView>& props,
        const std::unordered_set<uint64_t>& favorites,
        std::span<const SortSpec> sortOrder
    ) const;

    void ResetFilters();

private:
    [[nodiscard]] bool PassesTextFilter_(const PropView& view) const;
    [[nodiscard]] bool PassesSizeFilter_(const PropView& view) const;
    [[nodiscard]] bool PassesFavoritesOnlyFilter_(const PropView& view,
                                                  const std::unordered_set<uint64_t>& favorites) const;
    [[nodiscard]] static uint64_t MakePropKey_(const Prop& prop);
};
