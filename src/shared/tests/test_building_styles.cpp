#include <BuildingStyleResolver.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {
    Building MakeBuilding(const uint8_t purpose = 1) {
        Building building;
        building.instanceId = rfl::Hex<uint32_t>(1);
        building.groupId = rfl::Hex<uint32_t>(2);
        building.purposeType = purpose;
        return building;
    }
} // namespace

TEST_CASE("explicit building styles override occupant group styles", "[building-styles]") {
    auto building = MakeBuilding();
    building.occupantGroups = {0x2000};
    building.buildingStyles = std::vector<uint32_t>{0x20A0};

    REQUIRE(BuildingStyleResolver::SupportsStyle(building, 0x20A0));
    REQUIRE_FALSE(BuildingStyleResolver::SupportsStyle(building, 0x2000));
}

TEST_CASE("residential and commercial buildings fall back to occupant group styles", "[building-styles]") {
    auto building = MakeBuilding(2);
    building.occupantGroups = {0x2001};

    REQUIRE(BuildingStyleResolver::SupportsStyle(building, 0x2001));
    REQUIRE_FALSE(BuildingStyleResolver::SupportsStyle(building, 0x2002));
}

TEST_CASE("industrial buildings without valid explicit styles support every available style", "[building-styles]") {
    auto building = MakeBuilding(7);
    BuildingStyleContext context;
    context.availableStyleIds = {0x2000, 0x20A0};
    context.availabilityKnown = true;

    REQUIRE(BuildingStyleResolver::SupportsStyle(building, 0x20A0, context));
    REQUIRE_FALSE(BuildingStyleResolver::SupportsStyle(building, 0x20A1, context));
}

TEST_CASE("old PIMX 0x2004 placeholder falls back unless the marker is present", "[building-styles]") {
    auto building = MakeBuilding();
    building.occupantGroups = {0x2000};
    building.buildingStyles = std::vector<uint32_t>{0x2004};
    building.exemplarCategory = 0x2C8FBB95;

    REQUIRE_FALSE(BuildingStyleResolver::SupportsStyle(building, 0x2004));
    REQUIRE(BuildingStyleResolver::SupportsStyle(building, 0x2000));

    building.buildingStylesPimxTemplateMarker = false;
    REQUIRE(BuildingStyleResolver::SupportsStyle(building, 0x2004));
    REQUIRE_FALSE(BuildingStyleResolver::SupportsStyle(building, 0x2000));
}

TEST_CASE("explicit W2W false suppresses legacy occupant groups", "[building-styles]") {
    auto building = MakeBuilding();
    building.occupantGroups = {0xB5C00DDE};

    REQUIRE(BuildingStyleResolver::IsWallToWall(building));
    building.buildingIsWallToWall = false;
    REQUIRE_FALSE(BuildingStyleResolver::IsWallToWall(building));
    building.buildingIsWallToWall = true;
    REQUIRE(BuildingStyleResolver::IsWallToWall(building));
}

TEST_CASE("unavailable-only explicit styles fall back to legacy behavior", "[building-styles]") {
    auto building = MakeBuilding();
    building.occupantGroups = {0x2000};
    building.buildingStyles = std::vector<uint32_t>{0x20A0};

    BuildingStyleContext context;
    context.availableStyleIds = {0x2000};
    context.availabilityKnown = true;

    REQUIRE(BuildingStyleResolver::SupportsStyle(building, 0x2000, context));
    REQUIRE_FALSE(BuildingStyleResolver::SupportsStyle(building, 0x20A0, context));
}
