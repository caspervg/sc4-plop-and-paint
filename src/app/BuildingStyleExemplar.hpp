#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "ExemplarStructures.h"

constexpr auto kBuildingStylesPropertyId = 0xAA1DD400u;
constexpr auto kBuildingIsWallToWallPropertyId = 0xAA1DD401u;
constexpr auto kBuildingStylesPimxTemplateMarkerPropertyId = 0xAA1DD402u;
constexpr auto kBuildingPurposePropertyId = 0x27812833u;
constexpr auto kExemplarCategoryPropertyId = 0x2C8F8746u;

// Optional values keep an absent property apart from an empty or false one.
struct BuildingStyleExemplarData {
    std::optional<std::vector<uint32_t>> buildingStyles;
    std::optional<bool> buildingIsWallToWall;
    std::optional<uint8_t> purposeType;
    std::optional<uint32_t> exemplarCategory;
    std::optional<bool> buildingStylesPimxTemplateMarker;
};

// findProperty takes a property ID and returns a const Exemplar::Property*, or null when it is absent.
template <typename FindProperty>
[[nodiscard]] BuildingStyleExemplarData ReadBuildingStyleExemplarData(FindProperty&& findProperty) {
    BuildingStyleExemplarData data;

    if (const Exemplar::Property* prop = findProperty(kBuildingStylesPropertyId)) {
        // One style can be stored as a scalar. The More Building Styles DLL reads both forms.
        data.buildingStyles.emplace();
        data.buildingStyles->reserve(prop->values.size());
        for (size_t i = 0; i < prop->values.size(); ++i) {
            if (const auto style = prop->GetScalarAs<uint32_t>(i)) {
                data.buildingStyles->push_back(*style);
            }
        }
    }

    if (const Exemplar::Property* prop = findProperty(kBuildingIsWallToWallPropertyId)) {
        // Property presence suppresses the legacy occupant-group fallback,
        // even when the value is malformed. Match the DLL by treating that as false.
        // GetScalarAs converts integers to bool, so the type needs its own check.
        data.buildingIsWallToWall = prop->type == Exemplar::ValueType::Bool && prop->values.size() == 1 &&
            prop->GetScalarAs<bool>().value_or(false);
    }

    if (const Exemplar::Property* prop = findProperty(kBuildingPurposePropertyId)) {
        data.purposeType = prop->GetScalarAs<uint8_t>();
    }

    if (const Exemplar::Property* prop = findProperty(kExemplarCategoryPropertyId)) {
        data.exemplarCategory = prop->GetScalarAs<uint32_t>();
    }

    if (const Exemplar::Property* prop = findProperty(kBuildingStylesPimxTemplateMarkerPropertyId)) {
        // Only presence matters to the compatibility behavior.
        data.buildingStylesPimxTemplateMarker = prop->GetScalarAs<bool>().value_or(false);
    }

    return data;
}
