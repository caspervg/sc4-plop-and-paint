#include <SeasonalSetDetector.hpp>
#include <entities.hpp>
#include <rfl/cbor.hpp>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

Prop MakeTimedProp(const uint32_t gid, const uint32_t iid, std::string name,
                   const uint8_t month, const uint8_t day, const uint32_t duration) {
    Prop prop{};
    prop.groupId = rfl::Hex<uint32_t>(gid);
    prop.instanceId = rfl::Hex<uint32_t>(iid);
    prop.exemplarName = std::move(name);
    prop.simulatorDateStart = SimulatorDateStart{month, day};
    prop.simulatorDateDuration = duration;
    return prop;
}

std::vector<SeasonalSet> Detect(const std::vector<Prop>& props) {
    return seasonal::DetectSeasonalSets(props, {});
}

}  // namespace

TEST_CASE("SeasonalSet CBOR round-trip", "[cbor][seasonal]") {
    PropsCache cache;
    cache.seasonalSets.push_back(SeasonalSet{
        "CP Aspen Large",
        {rfl::Hex<uint32_t>(0x1000), rfl::Hex<uint32_t>(0x1001)},
        1,
        std::nullopt,
    });

    const auto bytes = rfl::cbor::write(cache);
    const auto restored = rfl::cbor::read<PropsCache>(bytes);
    REQUIRE(restored);
    REQUIRE(restored->seasonalSets.size() == 1);
    REQUIRE(restored->seasonalSets[0].name == "CP Aspen Large");
    REQUIRE(restored->seasonalSets[0].members.size() == 2);
    REQUIRE(restored->seasonalSets[0].members[1].value() == 0x1001);
    REQUIRE(restored->seasonalSets[0].confidence == 1);
}

TEST_CASE("Four-season CP-style set is detected via name stems", "[seasonal]") {
    const std::vector<Prop> props = {
        MakeTimedProp(0xA, 0x10, "CPOakPropSpring12x12x15", 3, 1, 92),
        MakeTimedProp(0xA, 0x11, "CPOakPropSummer12x12x15", 6, 1, 92),
        MakeTimedProp(0xA, 0x12, "CPOakPropFall12x12x15", 9, 1, 91),
        MakeTimedProp(0xA, 0x13, "CPOakPropWinter12x12x15", 12, 1, 90),
    };

    const auto sets = Detect(props);
    REQUIRE(sets.size() == 1);
    REQUIRE(sets[0].members.size() == 4);
    REQUIRE(sets[0].confidence == 0);
    // Members are ordered by window start within the year.
    REQUIRE(sets[0].members[0].value() == 0x10);
    REQUIRE(sets[0].members[3].value() == 0x13);
}

TEST_CASE("Three-season JENX-style set with long summer window", "[seasonal]") {
    const std::vector<Prop> props = {
        MakeTimedProp(0xB, 0x20, "JENXPROP_Tree51_Summer_seasonal", 3, 20, 195),
        MakeTimedProp(0xB, 0x21, "JENXPROP_Tree51_Fall_seasonal", 10, 1, 61),
        MakeTimedProp(0xB, 0x22, "JENXPROP_Tree51_Winter_seasonal", 12, 1, 110),
    };

    const auto sets = Detect(props);
    REQUIRE(sets.size() == 1);
    REQUIRE(sets[0].members.size() == 3);
}

TEST_CASE("Short-duration event props are not grouped", "[seasonal]") {
    // Maxis holiday gnomes: timed, similar names, but 4-day windows.
    const std::vector<Prop> props = {
        MakeTimedProp(0xC, 0x30, "LM1x1x3_Gnome_Alcatraz", 11, 22, 4),
        MakeTimedProp(0xC, 0x31, "LM1x1x3_Gnome_Alamo", 3, 2, 4),
        MakeTimedProp(0xC, 0x32, "LM1x1x3_Gnome_BigBen", 7, 12, 4),
    };

    REQUIRE(Detect(props).empty());
}

TEST_CASE("Same-stem props with heavily overlapping windows are rejected", "[seasonal]") {
    // Two summer variants of the same tree are alternatives, not a set.
    const std::vector<Prop> props = {
        MakeTimedProp(0xD, 0x40, "Tree_A_Summer", 5, 1, 120),
        MakeTimedProp(0xD, 0x41, "Tree_A_LateSummer", 6, 1, 120),
    };

    REQUIRE(Detect(props).empty());
}

TEST_CASE("Nested winter + snow overlay windows are tolerated", "[seasonal]") {
    // girafe/VIP pattern: Winter_Snow nests fully inside Winter.
    const std::vector<Prop> props = {
        MakeTimedProp(0xE, 0x50, "VIP_VV_Seasonal_Tree_007_Spring", 3, 23, 102),
        MakeTimedProp(0xE, 0x51, "VIP_VV_Seasonal_Tree_007_Summer", 7, 3, 65),
        MakeTimedProp(0xE, 0x52, "VIP_VV_Seasonal_Tree_007_Autumn", 9, 6, 74),
        MakeTimedProp(0xE, 0x53, "VIP_VV_Seasonal_Tree_007_Winter", 11, 19, 124),
        MakeTimedProp(0xE, 0x54, "VIP_VV_Seasonal_Tree_007_Winter_Snow", 12, 6, 40),
    };

    const auto sets = Detect(props);
    REQUIRE(sets.size() == 1);
    REQUIRE(sets[0].members.size() == 5);
}

TEST_CASE("Fuzzy pass attaches variant-suffixed singletons", "[seasonal]") {
    // Neko pattern: spring blooms carry _01 suffixes the summer/fall/winter
    // members lack, so strict stems differ but prefixes + windows line up.
    const std::vector<Prop> props = {
        MakeTimedProp(0xF, 0x60, "Neko_Cherry_Summer_A", 5, 1, 123),
        MakeTimedProp(0xF, 0x61, "Neko_Cherry_Fall_A", 9, 1, 61),
        MakeTimedProp(0xF, 0x62, "Neko_Cherry_Winter_A", 11, 1, 120),
        MakeTimedProp(0xF, 0x63, "Neko_Cherry_Spring_A_01", 3, 1, 61),
    };

    const auto sets = Detect(props);
    REQUIRE(sets.size() == 1);
    REQUIRE(sets[0].members.size() == 4);
    REQUIRE(sets[0].confidence == 1);
}

TEST_CASE("Fuzzy pass pairs two complementary singletons", "[seasonal]") {
    const std::vector<Prop> props = {
        MakeTimedProp(0x10, 0x70, "VV_inflatable_pool_01", 3, 1, 184),
        MakeTimedProp(0x10, 0x71, "VV_inflatable_pool_02", 9, 1, 181),
    };

    const auto sets = Detect(props);
    REQUIRE(sets.size() == 1);
    REQUIRE(sets[0].members.size() == 2);
    REQUIRE(sets[0].confidence == 1);
}

TEST_CASE("Props from different groups never merge", "[seasonal]") {
    const std::vector<Prop> props = {
        MakeTimedProp(0x11, 0x80, "Maple_Summer", 3, 1, 184),
        MakeTimedProp(0x22, 0x81, "Maple_Winter", 9, 1, 181),
    };

    REQUIRE(Detect(props).empty());
}

TEST_CASE("Malformed dates do not crash detection", "[seasonal]") {
    // Real catalog data: a Jack-O-Lantern with month 16 and an 780-day duration.
    const std::vector<Prop> props = {
        MakeTimedProp(0x12, 0x90, "Jack-O-Lantern Prop", 16, 1, 780),
        MakeTimedProp(0x12, 0x91, "Jack-O-Lantern Prop B", 1, 40, 30),
    };

    const auto sets = Detect(props);
    REQUIRE(sets.size() <= 1);
}

TEST_CASE("Set names are prettified from stems", "[seasonal]") {
    const std::vector<Prop> props = {
        MakeTimedProp(0x13, 0xA0, "Grfe_maple_A_summer", 3, 1, 184),
        MakeTimedProp(0x13, 0xA1, "Grfe_maple_A_autumn", 9, 1, 91),
        MakeTimedProp(0x13, 0xA2, "Grfe_maple_A_winter", 12, 1, 92),
    };

    const auto sets = Detect(props);
    REQUIRE(sets.size() == 1);
    REQUIRE(sets[0].name == "Grfe Maple A");
}
