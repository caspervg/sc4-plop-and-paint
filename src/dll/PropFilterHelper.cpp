#include "PropFilterHelper.hpp"

#include <algorithm>
#include <cctype>

bool PropFilterHelper::PassesFilters(const PropView& view) const {
    return PassesTextFilter_(view) && PassesSizeFilter_(view);
}

std::vector<PropView> PropFilterHelper::ApplyFiltersAndSort(
    const std::vector<PropView>& props,
    const std::unordered_set<uint64_t>& favorites,
    std::span<const SortSpec> sortOrder
) const {
    std::vector<PropView> filtered;
    filtered.reserve(props.size());

    for (const auto& view : props) {
        if (PassesFilters(view) && PassesFavoritesOnlyFilter_(view, favorites)) {
            filtered.push_back(view);
        }
    }

    const auto compareStrings = [](const std::string& lhs, const std::string& rhs) {
        if (lhs < rhs) return -1;
        if (lhs > rhs) return 1;
        return 0;
    };

    const auto compareFloats = [](const float lhs, const float rhs) {
        if (lhs < rhs) return -1;
        if (lhs > rhs) return 1;
        return 0;
    };

    std::vector<SortSpec> effectiveOrder;
    if (!sortOrder.empty()) {
        effectiveOrder.assign(sortOrder.begin(), sortOrder.end());
    }
    else {
        effectiveOrder.push_back({SortColumn::Name, false});
    }

    effectiveOrder.push_back({SortColumn::Name, false});

    std::ranges::sort(filtered, [&](const PropView& a, const PropView& b) {
        for (const auto& spec : effectiveOrder) {
            int cmp = 0;
            switch (spec.column) {
                case SortColumn::Name:
                    cmp = compareStrings(a.prop->visibleName, b.prop->visibleName);
                    break;
                case SortColumn::Size: {
                    const auto volumeA = a.prop->width * a.prop->height * a.prop->depth;
                    const auto volumeB = b.prop->width * b.prop->height * b.prop->depth;
                    cmp = compareFloats(volumeA, volumeB);
                    if (cmp == 0) cmp = compareFloats(a.prop->width, b.prop->width);
                    if (cmp == 0) cmp = compareFloats(a.prop->height, b.prop->height);
                    if (cmp == 0) cmp = compareFloats(a.prop->depth, b.prop->depth);
                    break;
                }
            }

            if (cmp != 0) {
                return spec.descending ? (cmp > 0) : (cmp < 0);
            }
        }

        return MakePropKey_(*a.prop) < MakePropKey_(*b.prop);
    });

    return filtered;
}

void PropFilterHelper::ResetFilters() {
    searchBuffer.clear();
    propWidth[0] = PropSize::kMinSize;
    propWidth[1] = PropSize::kMaxSize;
    propHeight[0] = PropSize::kMinSize;
    propHeight[1] = PropSize::kMaxSize;
    propDepth[0] = PropSize::kMinSize;
    propDepth[1] = PropSize::kMaxSize;
    favoritesOnly = false;
}

bool PropFilterHelper::PassesTextFilter_(const PropView& view) const {
    if (searchBuffer.empty()) {
        return true;
    }

    std::string searchLower;
    searchLower.reserve(searchBuffer.size());
    std::ranges::transform(searchBuffer, std::back_inserter(searchLower),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::string nameLower;
    nameLower.reserve(view.prop->visibleName.size());
    std::ranges::transform(view.prop->visibleName, std::back_inserter(nameLower),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (nameLower.find(searchLower) != std::string::npos) {
        return true;
    }

    std::string exemplarLower;
    exemplarLower.reserve(view.prop->exemplarName.size());
    std::ranges::transform(view.prop->exemplarName, std::back_inserter(exemplarLower),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return exemplarLower.find(searchLower) != std::string::npos;
}

bool PropFilterHelper::PassesSizeFilter_(const PropView& view) const {
    const auto minW = std::min(propWidth[0], propWidth[1]);
    const auto maxW = std::max(propWidth[0], propWidth[1]);
    const auto minH = std::min(propHeight[0], propHeight[1]);
    const auto maxH = std::max(propHeight[0], propHeight[1]);
    const auto minD = std::min(propDepth[0], propDepth[1]);
    const auto maxD = std::max(propDepth[0], propDepth[1]);

    return view.prop->width >= minW && view.prop->width <= maxW &&
           view.prop->height >= minH && view.prop->height <= maxH &&
           view.prop->depth >= minD && view.prop->depth <= maxD;
}

bool PropFilterHelper::PassesFavoritesOnlyFilter_(const PropView& view,
                                                  const std::unordered_set<uint64_t>& favorites) const {
    if (!favoritesOnly) {
        return true;
    }

    return favorites.contains(MakePropKey_(*view.prop));
}

uint64_t PropFilterHelper::MakePropKey_(const Prop& prop) {
    return (static_cast<uint64_t>(prop.groupId.value()) << 32) | prop.instanceId.value();
}
