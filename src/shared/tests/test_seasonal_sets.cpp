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

TEST_CASE("Mixed separators within one set do not split it", "[seasonal]") {
    // Real CP apple tree names: two members use "14_Prop", two use "14Prop".
    const std::vector<Prop> props = {
        MakeTimedProp(0xA, 0x20, "CP_Seas_Apple1_14_Prop_Spring", 2, 15, 89),
        MakeTimedProp(0xA, 0x21, "CP_Seas_Apple1_14Prop_Summer", 5, 15, 123),
        MakeTimedProp(0xA, 0x22, "CP_Seas_Apple1_14_Prop_Fall", 9, 15, 61),
        MakeTimedProp(0xA, 0x23, "CP_Seas_Apple1_14Prop_Winter", 11, 15, 92),
    };

    const auto sets = Detect(props);
    REQUIRE(sets.size() == 1);
    REQUIRE(sets[0].members.size() == 4);
    REQUIRE(sets[0].confidence == 0);
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

TEST_CASE("Snow overlay that sticks out of Winter is tolerated", "[seasonal]") {
    // Real VIP tree 001 dates: Winter_Snow starts before Winter and overlaps Autumn.
    const std::vector<Prop> props = {
        MakeTimedProp(0xE, 0x55, "VIP_VV_Seasonal_Tree_001_Spring", 3, 15, 100),
        MakeTimedProp(0xE, 0x56, "VIP_VV_Seasonal_Tree_001_Summer", 6, 23, 116),
        MakeTimedProp(0xE, 0x57, "VIP_VV_Seasonal_Tree_001_Autumn", 10, 17, 68),
        MakeTimedProp(0xE, 0x58, "VIP_VV_Seasonal_Tree_001_Winter", 12, 24, 81),
        MakeTimedProp(0xE, 0x59, "VIP_VV_Seasonal_Tree_001_Winter_Snow", 12, 6, 40),
    };

    const auto sets = Detect(props);
    REQUIRE(sets.size() == 1);
    REQUIRE(sets[0].members.size() == 5);
}

TEST_CASE("Fuzzy pairs that leave most of the year empty are dropped", "[seasonal]") {
    // Names match, but together they show for only 47 days a year.
    const std::vector<Prop> props = {
        MakeTimedProp(0x14, 0xB0, "SG_FarmField_Cane_01", 3, 1, 25),
        MakeTimedProp(0x14, 0xB1, "SG_FarmField_Cane_02", 6, 1, 22),
    };

    REQUIRE(Detect(props).empty());
}

TEST_CASE("Fuzzy pass does not pair different crops with a shared prefix", "[seasonal]") {
    // Real SG field props: complementary windows, but tulips and rice are not one plant.
    const std::vector<Prop> props = {
        MakeTimedProp(0x15, 0xC0, "SG_Field_FlowersBlue01", 3, 1, 92),
        MakeTimedProp(0x15, 0xC1, "SG_field_Rice01", 6, 1, 120),
    };

    REQUIRE(Detect(props).empty());
}

TEST_CASE("Fuzzy pass pairs abbreviated seasons and growth stages", "[seasonal]") {
    const std::vector<Prop> props = {
        MakeTimedProp(0x16, 0xD0, "fz_redcabbSPRI", 3, 1, 92),
        MakeTimedProp(0x16, 0xD1, "fz_redcabbSUM", 6, 1, 120),
        MakeTimedProp(0x17, 0xD2, "PEG-MTPA_Crop_Barley_Young_3.15-7.15", 3, 15, 122),
        MakeTimedProp(0x17, 0xD3, "PEG-MTPA_Crop_Barley_Mature_7.16-10.16", 7, 16, 92),
    };

    const auto sets = Detect(props);
    REQUIRE(sets.size() == 2);
    REQUIRE(sets[0].members.size() == 2);
    REQUIRE(sets[1].members.size() == 2);
}

TEST_CASE("German season names form a set", "[seasonal]") {
    const std::vector<Prop> props = {
        MakeTimedProp(0x18, 0xE0, "SFBT_Birke_Fruehling-big", 3, 1, 92),
        MakeTimedProp(0x18, 0xE1, "SFBT_Birke_Sommer-big", 6, 1, 92),
        MakeTimedProp(0x18, 0xE2, "SFBT_Birke_Herbst-big", 9, 1, 91),
        MakeTimedProp(0x18, 0xE3, "SFBT_Birke_Winter-big", 12, 1, 90),
    };

    const auto sets = Detect(props);
    REQUIRE(sets.size() == 1);
    REQUIRE(sets[0].members.size() == 4);
    REQUIRE(sets[0].confidence == 0);
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

TEST_CASE("Same stem across groups merges when the windows fill each other's gaps", "[seasonal]") {
    // Real Grfe beech: the autumn model ships in another group than summer and winter.
    const std::vector<Prop> props = {
        MakeTimedProp(0x11, 0x80, "Grfe_beech_A_summer", 3, 1, 184),
        MakeTimedProp(0x11, 0x81, "Grfe_beech_A_winter", 12, 1, 92),
        MakeTimedProp(0x22, 0x82, "Grfe_beech_A_autumn", 9, 1, 91),
    };

    const auto sets = Detect(props);
    REQUIRE(sets.size() == 1);
    REQUIRE(sets[0].members.size() == 3);
    REQUIRE(sets[0].confidence == 0);
}

TEST_CASE("Same stem across groups stays apart when the windows overlap", "[seasonal]") {
    // Two creators' oaks: each group is a set of its own.
    const std::vector<Prop> props = {
        MakeTimedProp(0x11, 0x83, "Oak_Summer", 3, 1, 184),
        MakeTimedProp(0x11, 0x84, "Oak_Winter", 9, 1, 181),
        MakeTimedProp(0x22, 0x85, "Oak_Summer", 4, 1, 184),
        MakeTimedProp(0x22, 0x86, "Oak_Winter", 10, 1, 181),
    };

    const auto sets = Detect(props);
    REQUIRE(sets.size() == 2);
    REQUIRE(sets[0].members.size() == 2);
    REQUIRE(sets[1].members.size() == 2);
}

TEST_CASE("Fuzzy matches never cross groups", "[seasonal]") {
    const std::vector<Prop> props = {
        MakeTimedProp(0x11, 0x87, "VV_inflatable_pool_01", 3, 1, 184),
        MakeTimedProp(0x22, 0x88, "VV_inflatable_pool_02", 9, 1, 181),
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
