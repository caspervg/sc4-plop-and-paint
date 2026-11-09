#include "app/BuildingStyleExemplar.hpp"

#include <catch2/catch_test_macros.hpp>
#include <map>
#include <utility>

namespace {
    using Properties = std::map<uint32_t, Exemplar::Property>;

    Exemplar::Property MakeProperty(const Exemplar::ValueType type, const bool isList,
                                    std::vector<Exemplar::ValueVariant> values) {
        Exemplar::Property property;
        property.type = type;
        property.isList = isList;
        property.values = std::move(values);
        return property;
    }

    BuildingStyleExemplarData Read(const Properties& properties) {
        return ReadBuildingStyleExemplarData([&](const uint32_t id) -> const Exemplar::Property* {
            const auto it = properties.find(id);
            return it == properties.end() ? nullptr : &it->second;
        });
    }
} // namespace

TEST_CASE("absent building style properties stay absent", "[parser][building-styles]") {
    const auto data = Read({});

    REQUIRE_FALSE(data.buildingStyles.has_value());
    REQUIRE_FALSE(data.buildingIsWallToWall.has_value());
    REQUIRE_FALSE(data.purposeType.has_value());
    REQUIRE_FALSE(data.exemplarCategory.has_value());
    REQUIRE_FALSE(data.buildingStylesPimxTemplateMarker.has_value());
}

TEST_CASE("building styles are read from a list", "[parser][building-styles]") {
    const auto data = Read({{kBuildingStylesPropertyId,
                             MakeProperty(Exemplar::ValueType::UInt32, true, {uint32_t{0x2000}, uint32_t{0x20A0}})}});

    REQUIRE(data.buildingStyles == std::vector<uint32_t>{0x2000, 0x20A0});
}

TEST_CASE("one building style stored as a scalar is read", "[parser][building-styles]") {
    const auto data =
        Read({{kBuildingStylesPropertyId, MakeProperty(Exemplar::ValueType::UInt32, false, {uint32_t{0x20A0}})}});

    REQUIRE(data.buildingStyles == std::vector<uint32_t>{0x20A0});
}

TEST_CASE("building styles without numbers are present and empty", "[parser][building-styles]") {
    auto data = Read({{kBuildingStylesPropertyId, MakeProperty(Exemplar::ValueType::UInt32, true, {})}});
    REQUIRE(data.buildingStyles == std::vector<uint32_t>{});

    data = Read({{kBuildingStylesPropertyId,
                  MakeProperty(Exemplar::ValueType::String, false, {std::string("not a style")})}});
    REQUIRE(data.buildingStyles == std::vector<uint32_t>{});
}

TEST_CASE("wall-to-wall property reads a bool and treats other types as false", "[parser][w2w]") {
    auto data = Read({{kBuildingIsWallToWallPropertyId, MakeProperty(Exemplar::ValueType::Bool, false, {true})}});
    REQUIRE(data.buildingIsWallToWall == true);

    data = Read({{kBuildingIsWallToWallPropertyId, MakeProperty(Exemplar::ValueType::Bool, false, {false})}});
    REQUIRE(data.buildingIsWallToWall == false);

    data = Read({{kBuildingIsWallToWallPropertyId, MakeProperty(Exemplar::ValueType::UInt32, false, {uint32_t{1}})}});
    REQUIRE(data.buildingIsWallToWall == false);

    data = Read({{kBuildingIsWallToWallPropertyId, MakeProperty(Exemplar::ValueType::Bool, true, {true, true})}});
    REQUIRE(data.buildingIsWallToWall == false);
}

TEST_CASE("purpose and exemplar category convert between integer types", "[parser][building-styles]") {
    const auto data = Read({
        {kBuildingPurposePropertyId, MakeProperty(Exemplar::ValueType::UInt32, false, {uint32_t{7}})},
        {kExemplarCategoryPropertyId, MakeProperty(Exemplar::ValueType::UInt32, false, {uint32_t{0x2C8FBB95}})},
    });

    REQUIRE(data.purposeType == uint8_t{7});
    REQUIRE(data.exemplarCategory == uint32_t{0x2C8FBB95});
}

TEST_CASE("PIMX marker counts as present whatever its value", "[parser][building-styles]") {
    const auto data = Read({{kBuildingStylesPimxTemplateMarkerPropertyId,
                             MakeProperty(Exemplar::ValueType::UInt32, false, {uint32_t{1}})}});

    REQUIRE(data.buildingStylesPimxTemplateMarker.has_value());
}
