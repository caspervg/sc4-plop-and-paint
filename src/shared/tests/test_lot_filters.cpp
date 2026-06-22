#include "dll/lots/LotFilterHelper.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {
    struct TestLotView {
        Building building;
        Lot lot;

        TestLotView() {
            building.instanceId = rfl::Hex<uint32_t>(1);
            building.groupId = rfl::Hex<uint32_t>(2);
            building.name = "Building";
            building.purposeType = 1;
            lot.instanceId = rfl::Hex<uint32_t>(3);
            lot.groupId = rfl::Hex<uint32_t>(4);
            lot.name = "Lot";
            lot.sizeX = 1;
            lot.sizeZ = 1;
            lot.growthStage = 1;
        }

        [[nodiscard]] LotView View() const { return {&building, &lot}; }
    };
} // namespace

TEST_CASE("lot style filter uses any selected style", "[lot-filter][building-styles]") {
    TestLotView item;
    item.building.buildingStyles = std::vector<uint32_t>{0x2000, 0x20A0};

    LotFilterHelper filter;
    filter.selectedBuildingStyles = {0x2001, 0x20A0};
    REQUIRE(filter.PassesFilters(item.View()));

    filter.selectedBuildingStyles = {0x2001};
    REQUIRE_FALSE(filter.PassesFilters(item.View()));
}

TEST_CASE("lot W2W filter supports both inclusion and exclusion", "[lot-filter][w2w]") {
    TestLotView item;
    item.building.buildingIsWallToWall = true;

    LotFilterHelper filter;
    filter.wallToWallFilter = LotFilterHelper::WallToWallFilter::WallToWallOnly;
    REQUIRE(filter.PassesFilters(item.View()));

    filter.wallToWallFilter = LotFilterHelper::WallToWallFilter::NonWallToWallOnly;
    REQUIRE_FALSE(filter.PassesFilters(item.View()));
}

TEST_CASE("active style filter follows active style context", "[lot-filter][building-styles]") {
    TestLotView item;
    item.building.buildingStyles = std::vector<uint32_t>{0x20A0};

    LotFilterHelper filter;
    filter.activeStylesOnly = true;

    BuildingStyleContext context;
    context.activeStylesKnown = true;
    context.activeStyleIds = {0x2000};
    REQUIRE_FALSE(filter.PassesFilters(item.View(), context));

    context.activeStyleIds.insert(0x20A0);
    REQUIRE(filter.PassesFilters(item.View(), context));
}
