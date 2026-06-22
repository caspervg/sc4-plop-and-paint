#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_set>

#include "entities.hpp"

struct BuildingStyleContext {
    std::unordered_set<uint32_t> availableStyleIds;
    std::unordered_set<uint32_t> wallToWallOccupantGroups;
    std::unordered_set<uint32_t> activeStyleIds;
    bool availabilityKnown = false;
    bool activeStylesKnown = false;
    bool useAllStylesAtOnce = false;
};

class BuildingStyleResolver {
public:
    [[nodiscard]] static bool IsWallToWall(const Building& building, const BuildingStyleContext& context = {}) {
        if (building.buildingIsWallToWall.has_value()) {
            return *building.buildingIsWallToWall;
        }

        if (!context.wallToWallOccupantGroups.empty()) {
            return std::ranges::any_of(building.occupantGroups, [&](const uint32_t group) {
                return context.wallToWallOccupantGroups.contains(group);
            });
        }

        return std::ranges::any_of(building.occupantGroups, [](const uint32_t group) {
            return Contains(kLegacyWallToWallOccupantGroups, group);
        });
    }

    [[nodiscard]] static bool SupportsStyle(const Building& building, const uint32_t styleId,
                                            const BuildingStyleContext& context = {}) {
        if (UsesExplicitStyles(building, context)) {
            return std::ranges::find(*building.buildingStyles, styleId) != building.buildingStyles->end() &&
                IsUsableExplicitStyle(styleId, context);
        }

        if (IsIndustrial(building.purposeType)) {
            return !context.availabilityKnown || context.availableStyleIds.contains(styleId);
        }

        if (!SupportsStyles(building.purposeType)) {
            return false;
        }

        return building.occupantGroups.contains(styleId);
    }

    [[nodiscard]] static bool SupportsAnyStyle(const Building& building, const std::unordered_set<uint32_t>& styleIds,
                                               const BuildingStyleContext& context = {}) {
        return std::ranges::any_of(styleIds,
                                   [&](const uint32_t styleId) { return SupportsStyle(building, styleId, context); });
    }

    [[nodiscard]] static bool UsesExplicitStyles(const Building& building, const BuildingStyleContext& context = {}) {
        if (!building.buildingStyles.has_value() || IsOldPimxStylePlaceholder(building)) {
            return false;
        }

        return std::ranges::any_of(*building.buildingStyles,
                                   [&](const uint32_t styleId) { return IsUsableExplicitStyle(styleId, context); });
    }

    [[nodiscard]] static bool IsReservedStyleId(const uint32_t styleId) {
        return styleId <= 0x10Fu || styleId == 0x7B6BC069u || Contains(kMaxisUiControlIds, styleId) ||
            Contains(kOptionalButtonIds, styleId);
    }

private:
    [[nodiscard]] static bool IsUsableExplicitStyle(const uint32_t styleId, const BuildingStyleContext& context) {
        return !IsReservedStyleId(styleId) &&
            (!context.availabilityKnown || context.availableStyleIds.contains(styleId));
    }

    [[nodiscard]] static bool SupportsStyles(const std::optional<uint8_t> purposeType) {
        // Older caches do not contain the building purpose. In that case,
        // preserve useful legacy occupant-group matching instead of failing closed.
        return !purposeType.has_value() || (*purposeType >= 1 && *purposeType <= 8 && *purposeType != 4);
    }

    [[nodiscard]] static bool IsIndustrial(const std::optional<uint8_t> purposeType) {
        return purposeType.has_value() && *purposeType >= 5 && *purposeType <= 8;
    }

    [[nodiscard]] static bool IsOldPimxStylePlaceholder(const Building& building) {
        if (!building.buildingStyles.has_value() || building.buildingStyles->size() != 1 ||
            building.buildingStyles->front() != 0x2004u || building.buildingStylesPimxTemplateMarker.has_value() ||
            !building.exemplarCategory.has_value()) {
            return false;
        }

        return Contains(kPimxBuildingTemplateIds, *building.exemplarCategory);
    }

    template <size_t N>
    [[nodiscard]] static constexpr bool Contains(const std::array<uint32_t, N>& values, const uint32_t value) {
        return std::ranges::find(values, value) != values.end();
    }

    static constexpr std::array<uint32_t, 6> kLegacyWallToWallOccupantGroups{0xD02C802Eu, 0xB5C00A05u, 0xB5C00B05u,
                                                                             0xB5C00DDEu, 0xB5C00F0Au, 0xB5C00F0Bu};

    static constexpr std::array<uint32_t, 6> kMaxisUiControlIds{0xCBC61559u, 0xEBC61560u, 0x0BC61548u,
                                                                0x2BC619F3u, 0xCBC61567u, 0xEBC619FDu};

    static constexpr std::array<uint32_t, 6> kOptionalButtonIds{0x9476D8DAu, 0xB510A368u, 0x31150389u,
                                                                0x3115038Au, 0x3115038Bu, 0x3621731Bu};

    static constexpr std::array<uint32_t, 24> kPimxBuildingTemplateIds{
        0x2C8FBB95u, 0xF6A23754u, 0x6C8FBBA5u, 0xCE253A47u, 0x0C8FBBAEu, 0x4EC7D949u, 0x8C8FBBCCu, 0xBCD2C7E1u,
        0x0C8FBBDCu, 0x539F3C1Du, 0xAC8FBBEBu, 0x3B3F3641u, 0x6C8FBBF5u, 0x452925B6u, 0xCC8FBC01u, 0x8840BC41u,
        0x2CAA4D2Au, 0xC035B844u, 0x2C8FBC17u, 0x4B2D4F0Bu, 0x6C7E983Bu, 0xEE28B711u, 0x6C8FBDDCu, 0x3B238BC0u};
};
