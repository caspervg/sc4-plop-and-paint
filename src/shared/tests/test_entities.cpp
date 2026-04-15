#include <algorithm>
#include <entities.hpp>
#include <rfl/cbor.hpp>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>

inline bool operator==(const Lot& lhs, const Lot& rhs);
inline bool operator==(const FamilyEntry& lhs, const FamilyEntry& rhs);
inline bool operator==(const PropFamily& lhs, const PropFamily& rhs);
inline bool operator==(const RecentPaintEntryData& lhs, const RecentPaintEntryData& rhs);
inline bool operator==(const PropTimeOfDay& lhs, const PropTimeOfDay& rhs);
inline bool operator==(const SimulatorDateStart& lhs, const SimulatorDateStart& rhs);
inline bool operator==(const Prop& lhs, const Prop& rhs);

// Helper function to compare Icon structs
inline bool operator==(const Icon& lhs, const Icon& rhs) {
    return lhs.data == rhs.data && lhs.width == rhs.width && lhs.height == rhs.height;
}

// Helper function to compare PreRendered structs
inline bool operator==(const PreRendered& lhs, const PreRendered& rhs) {
    return lhs.data == rhs.data && lhs.width == rhs.width && lhs.height == rhs.height;
}

// Helper function to compare Building structs
inline bool operator==(const Building& lhs, const Building& rhs) {
    return lhs.instanceId.value() == rhs.instanceId.value() &&
           lhs.groupId.value() == rhs.groupId.value() &&
           lhs.name == rhs.name &&
           lhs.description == rhs.description &&
           lhs.occupantGroups == rhs.occupantGroups &&
           lhs.thumbnail == rhs.thumbnail &&
           lhs.lots == rhs.lots;
}

// Helper function to compare Lot structs
inline bool operator==(const Lot& lhs, const Lot& rhs) {
    return lhs.instanceId.value() == rhs.instanceId.value() &&
           lhs.groupId.value() == rhs.groupId.value() &&
           lhs.name == rhs.name &&
           lhs.sizeX == rhs.sizeX &&
           lhs.sizeZ == rhs.sizeZ &&
           lhs.minCapacity == rhs.minCapacity &&
           lhs.maxCapacity == rhs.maxCapacity &&
           lhs.growthStage == rhs.growthStage &&
           lhs.zoneType == rhs.zoneType &&
           lhs.wealthType == rhs.wealthType &&
           lhs.purposeType == rhs.purposeType;
}

// Helper function to compare FamilyEntry structs
inline bool operator==(const FamilyEntry& lhs, const FamilyEntry& rhs) {
    return lhs.propID.value() == rhs.propID.value() &&
           lhs.weight == rhs.weight;
}

// Helper function to compare PropFamily structs
inline bool operator==(const PropFamily& lhs, const PropFamily& rhs) {
    const bool samePersistentId =
        lhs.persistentId.has_value() == rhs.persistentId.has_value() &&
        (!lhs.persistentId.has_value() || lhs.persistentId->value() == rhs.persistentId->value());
    return lhs.name == rhs.name &&
           lhs.entries == rhs.entries &&
           lhs.densityVariation == rhs.densityVariation &&
           samePersistentId;
}

inline bool operator==(const RecentPaintEntryData& lhs, const RecentPaintEntryData& rhs) {
    return lhs.sourceKind == rhs.sourceKind &&
           lhs.sourceId.value() == rhs.sourceId.value() &&
           lhs.kind == rhs.kind &&
           lhs.typeId.value() == rhs.typeId.value() &&
           lhs.thumbnailKey.value() == rhs.thumbnailKey.value() &&
           lhs.name == rhs.name &&
           lhs.palette == rhs.palette;
}

inline bool operator==(const PropTimeOfDay& lhs, const PropTimeOfDay& rhs) {
    return lhs.startHour == rhs.startHour &&
           lhs.endHour == rhs.endHour;
}

inline bool operator==(const SimulatorDateStart& lhs, const SimulatorDateStart& rhs) {
    return lhs.month == rhs.month &&
           lhs.day == rhs.day;
}

inline bool operator==(const Prop& lhs, const Prop& rhs) {
    const bool sameFamilyIds =
        lhs.familyIds.size() == rhs.familyIds.size() &&
        std::equal(lhs.familyIds.begin(), lhs.familyIds.end(), rhs.familyIds.begin(),
            [](const auto& left, const auto& right) { return left.value() == right.value(); });
    return lhs.groupId.value() == rhs.groupId.value() &&
           lhs.instanceId.value() == rhs.instanceId.value() &&
           lhs.exemplarName == rhs.exemplarName &&
           lhs.visibleName == rhs.visibleName &&
           lhs.width == rhs.width &&
           lhs.height == rhs.height &&
           lhs.depth == rhs.depth &&
           lhs.minX == rhs.minX &&
           lhs.maxX == rhs.maxX &&
           lhs.minY == rhs.minY &&
           lhs.maxY == rhs.maxY &&
           lhs.minZ == rhs.minZ &&
           lhs.maxZ == rhs.maxZ &&
           sameFamilyIds &&
           lhs.nighttimeStateChange == rhs.nighttimeStateChange &&
           lhs.timeOfDay == rhs.timeOfDay &&
           lhs.simulatorDateStart == rhs.simulatorDateStart &&
           lhs.simulatorDateDuration == rhs.simulatorDateDuration &&
           lhs.simulatorDateInterval == rhs.simulatorDateInterval &&
           lhs.randomChance == rhs.randomChance &&
           lhs.thumbnail == rhs.thumbnail;
}

// Helper function to compare TabFavorites structs
inline bool operator==(const TabFavorites& lhs, const TabFavorites& rhs) {
    if (lhs.items.size() != rhs.items.size()) return false;
    for (size_t i = 0; i < lhs.items.size(); ++i) {
        if (lhs.items[i].value() != rhs.items[i].value()) return false;
    }
    return true;
}

// Helper function to compare AllFavorites structs
inline bool operator==(const AllFavorites& lhs, const AllFavorites& rhs) {
    return lhs.version == rhs.version &&
           lhs.lots == rhs.lots &&
           lhs.props == rhs.props &&
           lhs.flora == rhs.flora &&
           lhs.families == rhs.families &&
           lhs.recentPaints == rhs.recentPaints &&
           lhs.lastModified.str() == rhs.lastModified.str();
}

TEST_CASE("Icon CBOR serialization and deserialization", "[cbor][icon]") {
    Icon original{
        .data = rfl::Bytestring(std::vector<std::byte>{std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}}),
        .width = 256,
        .height = 128
    };

    auto cbor_bytes = rfl::cbor::write(original);
    REQUIRE(!cbor_bytes.empty());

    auto deserialized = rfl::cbor::read<Icon>(cbor_bytes);
    REQUIRE(deserialized);
    REQUIRE(*deserialized == original);
}

TEST_CASE("PreRendered CBOR serialization and deserialization", "[cbor][prerendered]") {
    PreRendered original{
        .data = rfl::Bytestring(std::vector<std::byte>{std::byte{0xFF}, std::byte{0x00}, std::byte{0xFF}}),
        .width = 512,
        .height = 512
    };

    auto cbor_bytes = rfl::cbor::write(original);
    REQUIRE(!cbor_bytes.empty());

    auto deserialized = rfl::cbor::read<PreRendered>(cbor_bytes);
    REQUIRE(deserialized);
    REQUIRE(*deserialized == original);
}

TEST_CASE("Thumbnail CBOR serialization with Icon", "[cbor][thumbnail]") {
    Icon icon_data{
        .data = rfl::Bytestring(std::vector<std::byte>{std::byte{0x11}, std::byte{0x22}}),
        .width = 64,
        .height = 64
    };
    Thumbnail original = icon_data;

    auto cbor_bytes = rfl::cbor::write(original);
    REQUIRE(!cbor_bytes.empty());

    auto deserialized = rfl::cbor::read<Thumbnail>(cbor_bytes);
    REQUIRE(deserialized);
    REQUIRE(*deserialized == original);
}

TEST_CASE("Thumbnail CBOR serialization with PreRendered", "[cbor][thumbnail]") {
    PreRendered prerendered{
        .data = rfl::Bytestring(std::vector<std::byte>{std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}}),
        .width = 128,
        .height = 256
    };
    Thumbnail original = prerendered;

    auto cbor_bytes = rfl::cbor::write(original);
    REQUIRE(!cbor_bytes.empty());

    auto deserialized = rfl::cbor::read<Thumbnail>(cbor_bytes);
    REQUIRE(deserialized);
    REQUIRE(*deserialized == original);
}

TEST_CASE("Building CBOR serialization and deserialization", "[cbor][building]") {
    Building original{
        .instanceId = rfl::Hex<uint32_t>(0x12345678),
        .groupId = rfl::Hex<uint32_t>(0x87654321),
        .name = "Test Building",
        .description = "A test building for CBOR serialization",
        .occupantGroups = {0xDEADBEEF, 0xCAFEBABE},
        .thumbnail = Thumbnail{Icon{
            .data = rfl::Bytestring(std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}}),
            .width = 256,
            .height = 128
        }},
        .lots = {
            Lot{
                .instanceId = rfl::Hex<uint32_t>(0x01020304),
                .groupId = rfl::Hex<uint32_t>(0x11111111),
                .name = "Stage 1 Lot",
                .sizeX = 2,
                .sizeZ = 3,
                .minCapacity = 50,
                .maxCapacity = 120,
                .growthStage = 1,
                .zoneType = 2,
                .wealthType = 1,
                .purposeType = 4
            }
        }
    };

    auto cbor_bytes = rfl::cbor::write(original);
    REQUIRE(!cbor_bytes.empty());

    auto deserialized = rfl::cbor::read<Building>(cbor_bytes);
    REQUIRE(deserialized);
    REQUIRE(*deserialized == original);
}

TEST_CASE("Lot CBOR serialization and deserialization", "[cbor][lot]") {
    Lot original{
        .instanceId = rfl::Hex<uint32_t>(0xAABBCCDD),
        .groupId = rfl::Hex<uint32_t>(0xDDCCBBAA),
        .name = "Test Lot",
        .sizeX = 2,
        .sizeZ = 4,
        .minCapacity = 100,
        .maxCapacity = 500,
        .growthStage = 3,
        .zoneType = 7,
        .wealthType = 2,
        .purposeType = 5
    };

    auto cbor_bytes = rfl::cbor::write(original);
    REQUIRE(!cbor_bytes.empty());

    auto deserialized = rfl::cbor::read<Lot>(cbor_bytes);
    REQUIRE(deserialized);
    REQUIRE(*deserialized == original);
}

TEST_CASE("Prop CBOR serialization and deserialization with timed metadata", "[cbor][prop]") {
    Prop original{
        .groupId = rfl::Hex<uint32_t>(0x11223344),
        .instanceId = rfl::Hex<uint32_t>(0x55667788),
        .exemplarName = "Timed Prop",
        .visibleName = "Timed Prop Visible",
        .width = 3.5f,
        .height = 5.25f,
        .depth = 2.0f,
        .minX = -1.75f,
        .maxX = 1.75f,
        .minY = 0.0f,
        .maxY = 5.25f,
        .minZ = -1.0f,
        .maxZ = 1.0f,
        .familyIds = {rfl::Hex<uint32_t>(0xAABBCCDD)},
        .nighttimeStateChange = true,
        .timeOfDay = PropTimeOfDay{6.0f, 18.5f},
        .simulatorDateStart = SimulatorDateStart{4, 15},
        .simulatorDateDuration = 120,
        .simulatorDateInterval = 365,
        .randomChance = 40,
        .thumbnail = std::nullopt
    };

    auto cbor_bytes = rfl::cbor::write(original);
    REQUIRE(!cbor_bytes.empty());

    auto deserialized = rfl::cbor::read<Prop>(cbor_bytes);
    REQUIRE(deserialized);
    REQUIRE(*deserialized == original);
}

TEST_CASE("Empty occupant groups CBOR serialization", "[cbor][edge-case]") {
    Building original{
        .instanceId = rfl::Hex<uint32_t>(0x11111111),
        .groupId = rfl::Hex<uint32_t>(0x22222222),
        .name = "Empty Groups",
        .description = "No occupant groups",
        .occupantGroups = {},
        .thumbnail = std::nullopt,
        .lots = {}
    };

    auto cbor_bytes = rfl::cbor::write(original);
    auto deserialized = rfl::cbor::read<Building>(cbor_bytes);
    REQUIRE(deserialized);
    REQUIRE(*deserialized == original);
}

TEST_CASE("Large occupant groups CBOR serialization", "[cbor][edge-case]") {
    std::unordered_set<uint32_t> large_groups;
    for (uint32_t i = 0; i < 100; ++i) {
        large_groups.insert(0x10000000 + i);
    }

    Building original{
        .instanceId = rfl::Hex<uint32_t>(0x33333333),
        .groupId = rfl::Hex<uint32_t>(0x44444444),
        .name = "Large Building",
        .description = "Building with many occupant groups",
        .occupantGroups = large_groups,
        .thumbnail = Thumbnail{Icon{
            .data = rfl::Bytestring(std::vector<std::byte>{std::byte{0x99}}),
            .width = 128,
            .height = 128
        }},
        .lots = {}
    };

    auto cbor_bytes = rfl::cbor::write(original);
    REQUIRE(!cbor_bytes.empty());

    auto deserialized = rfl::cbor::read<Building>(cbor_bytes);
    REQUIRE(deserialized);
    REQUIRE(*deserialized == original);
}

TEST_CASE("Large binary data CBOR serialization", "[cbor][edge-case]") {
    std::vector<std::byte> large_data;
    for (int i = 0; i < 1000; ++i) {
        large_data.push_back(std::byte(i % 256));
    }

    Icon original{
        .data = rfl::Bytestring(large_data),
        .width = 1024,
        .height = 1024
    };

    auto cbor_bytes = rfl::cbor::write(original);
    REQUIRE(!cbor_bytes.empty());

    auto deserialized = rfl::cbor::read<Icon>(cbor_bytes);
    REQUIRE(deserialized);
    REQUIRE(*deserialized == original);
}

TEST_CASE("TabFavorites CBOR serialization and deserialization", "[cbor][favorites]") {
    TabFavorites original{
        .items = {rfl::Hex<uint64_t>(0xAABBCCDDULL), rfl::Hex<uint64_t>(0x123456789ABCDEF0ULL)}
    };

    auto cbor_bytes = rfl::cbor::write(original);
    REQUIRE(!cbor_bytes.empty());

    auto deserialized = rfl::cbor::read<TabFavorites>(cbor_bytes);
    REQUIRE(deserialized);
    REQUIRE(deserialized->items.size() == 2);
    REQUIRE(deserialized->items[0].value() == 0xAABBCCDDULL);
    REQUIRE(deserialized->items[1].value() == 0x123456789ABCDEF0ULL);
}

TEST_CASE("AllFavorites CBOR serialization with lots only", "[cbor][favorites]") {
    AllFavorites original{
        .version = 2,
        .lots = {.items = {rfl::Hex<uint64_t>(0xAABBCCDDULL), rfl::Hex<uint64_t>(0x123456789ABCDEF0ULL)}},
        .props = std::nullopt,
        .flora = std::nullopt,
        .families = std::nullopt,
        .lastModified = rfl::Timestamp<"%Y-%m-%dT%H:%M:%S">("2026-01-20T10:30:00")
    };

    auto cbor_bytes = rfl::cbor::write(original);
    REQUIRE(!cbor_bytes.empty());

    auto deserialized = rfl::cbor::read<AllFavorites>(cbor_bytes);
    REQUIRE(deserialized);
    REQUIRE(deserialized->version == 2);
    REQUIRE(deserialized->lots.items.size() == 2);
    REQUIRE(deserialized->lots.items[0].value() == 0xAABBCCDDULL);
    REQUIRE(deserialized->lots.items[1].value() == 0x123456789ABCDEF0ULL);
    REQUIRE(!deserialized->props.has_value());
    REQUIRE(!deserialized->flora.has_value());
    REQUIRE(!deserialized->families.has_value());
    REQUIRE(deserialized->lastModified.str() == "2026-01-20T10:30:00");
}

TEST_CASE("AllFavorites CBOR serialization with all sections", "[cbor][favorites]") {
    AllFavorites original{
        .version = 2,
        .lots = {.items = {rfl::Hex<uint64_t>(0x11111111ULL)}},
        .props = TabFavorites{.items = {rfl::Hex<uint64_t>(0x22222222ULL)}},
        .flora = TabFavorites{.items = {rfl::Hex<uint64_t>(0x33333333ULL)}},
        .families = std::vector<PropFamily>{
            PropFamily{
                .name = "Street Furniture",
                .entries = {
                    FamilyEntry{.propID = rfl::Hex<uint32_t>(0x01020304), .weight = 1.0f},
                    FamilyEntry{.propID = rfl::Hex<uint32_t>(0x05060708), .weight = 2.5f}
                },
                .densityVariation = 0.35f,
                .persistentId = rfl::Hex<uint64_t>(0xABCDEF01ULL)
            }
        },
        .recentPaints = std::vector<RecentPaintEntryData>{
            RecentPaintEntryData{
                .sourceKind = 2,
                .sourceId = rfl::Hex<uint64_t>(0xABCDEF01ULL),
                .kind = 0,
                .typeId = rfl::Hex<uint32_t>(0x01020304),
                .thumbnailKey = rfl::Hex<uint64_t>(0x1111222233334444ULL),
                .name = "Street Furniture",
                .palette = {
                    FamilyEntry{.propID = rfl::Hex<uint32_t>(0x01020304), .weight = 1.0f},
                    FamilyEntry{.propID = rfl::Hex<uint32_t>(0x05060708), .weight = 2.5f}
                }
            }
        },
        .lastModified = rfl::Timestamp<"%Y-%m-%dT%H:%M:%S">("2026-01-20T15:45:30")
    };

    auto cbor_bytes = rfl::cbor::write(original);
    REQUIRE(!cbor_bytes.empty());

    auto deserialized = rfl::cbor::read<AllFavorites>(cbor_bytes);
    REQUIRE(deserialized);
    REQUIRE(deserialized->version == 2);
    REQUIRE(deserialized->lots.items.size() == 1);
    REQUIRE(deserialized->props.has_value());
    REQUIRE(deserialized->props->items.size() == 1);
    REQUIRE(deserialized->props->items[0].value() == 0x22222222ULL);
    REQUIRE(deserialized->flora.has_value());
    REQUIRE(deserialized->flora->items.size() == 1);
    REQUIRE(deserialized->flora->items[0].value() == 0x33333333ULL);
    REQUIRE(deserialized->families.has_value());
    REQUIRE(deserialized->families->size() == 1);
    REQUIRE((*deserialized->families)[0].name == "Street Furniture");
    REQUIRE((*deserialized->families)[0].entries.size() == 2);
    REQUIRE((*deserialized->families)[0].persistentId.has_value());
    REQUIRE((*deserialized->families)[0].persistentId->value() == 0xABCDEF01ULL);
    REQUIRE(deserialized->recentPaints.has_value());
    REQUIRE(deserialized->recentPaints->size() == 1);
    REQUIRE((*deserialized->recentPaints)[0].sourceId.value() == 0xABCDEF01ULL);
    REQUIRE((*deserialized->recentPaints)[0].palette.size() == 2);
}

TEST_CASE("AllFavorites CBOR empty favorites", "[cbor][favorites][edge-case]") {
    AllFavorites original{
        .version = 2,
        .lots = {.items = {}},
        .props = std::nullopt,
        .flora = std::nullopt,
        .families = std::nullopt,
        .lastModified = rfl::Timestamp<"%Y-%m-%dT%H:%M:%S">("2026-01-20T00:00:00")
    };

    auto cbor_bytes = rfl::cbor::write(original);
    REQUIRE(!cbor_bytes.empty());

    auto deserialized = rfl::cbor::read<AllFavorites>(cbor_bytes);
    REQUIRE(deserialized);
    REQUIRE(deserialized->lots.items.empty());
    REQUIRE(!deserialized->props.has_value());
    REQUIRE(!deserialized->flora.has_value());
    REQUIRE(!deserialized->families.has_value());
}
