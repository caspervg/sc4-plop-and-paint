#include <entities.hpp>
#include <rfl/cbor.hpp>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>

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
           lhs.thumbnail == rhs.thumbnail;
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
           lhs.building == rhs.building;
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
        .thumbnail = Icon{
            .data = rfl::Bytestring(std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}}),
            .width = 256,
            .height = 128
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
        .building = Building{
            .instanceId = rfl::Hex<uint32_t>(0x11223344),
            .groupId = rfl::Hex<uint32_t>(0x44332211),
            .name = "Lot Building",
            .description = "Building on a lot",
            .occupantGroups = {0xBEEFCAFE},
            .thumbnail = PreRendered{
                .data = rfl::Bytestring(std::vector<std::byte>{std::byte{0xFF}, std::byte{0xEE}}),
                .width = 512,
                .height = 512
            }
        }
    };

    auto cbor_bytes = rfl::cbor::write(original);
    REQUIRE(!cbor_bytes.empty());

    auto deserialized = rfl::cbor::read<Lot>(cbor_bytes);
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
        .thumbnail = Icon{
            .data = rfl::Bytestring(std::vector<std::byte>{}),
            .width = 0,
            .height = 0
        }
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
        .thumbnail = Icon{
            .data = rfl::Bytestring(std::vector<std::byte>{std::byte{0x99}}),
            .width = 128,
            .height = 128
        }
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
        .items = {rfl::Hex<uint32_t>(0xAABBCCDD), rfl::Hex<uint32_t>(0x12345678)}
    };

    auto cbor_bytes = rfl::cbor::write(original);
    REQUIRE(!cbor_bytes.empty());

    auto deserialized = rfl::cbor::read<TabFavorites>(cbor_bytes);
    REQUIRE(deserialized);
    REQUIRE(deserialized->items.size() == 2);
    REQUIRE(deserialized->items[0].value() == 0xAABBCCDD);
    REQUIRE(deserialized->items[1].value() == 0x12345678);
}

TEST_CASE("AllFavorites CBOR serialization with lots only", "[cbor][favorites]") {
    AllFavorites original{
        .version = 1,
        .lots = {.items = {rfl::Hex<uint32_t>(0xAABBCCDD), rfl::Hex<uint32_t>(0x12345678)}},
        .props = std::nullopt,
        .flora = std::nullopt,
        .lastModified = rfl::Timestamp<"%Y-%m-%dT%H:%M:%S">::from_string("2026-01-20T10:30:00")
    };

    auto cbor_bytes = rfl::cbor::write(original);
    REQUIRE(!cbor_bytes.empty());

    auto deserialized = rfl::cbor::read<AllFavorites>(cbor_bytes);
    REQUIRE(deserialized);
    REQUIRE(deserialized->version == 1);
    REQUIRE(deserialized->lots.items.size() == 2);
    REQUIRE(deserialized->lots.items[0].value() == 0xAABBCCDD);
    REQUIRE(deserialized->lots.items[1].value() == 0x12345678);
    REQUIRE(!deserialized->props.has_value());
    REQUIRE(!deserialized->flora.has_value());
    REQUIRE(deserialized->lastModified.str() == "2026-01-20T10:30:00");
}

TEST_CASE("AllFavorites CBOR serialization with all sections", "[cbor][favorites]") {
    AllFavorites original{
        .version = 1,
        .lots = {.items = {rfl::Hex<uint32_t>(0x11111111)}},
        .props = TabFavorites{.items = {rfl::Hex<uint32_t>(0x22222222)}},
        .flora = TabFavorites{.items = {rfl::Hex<uint32_t>(0x33333333)}},
        .lastModified = rfl::Timestamp<"%Y-%m-%dT%H:%M:%S">::from_string("2026-01-20T15:45:30")
    };

    auto cbor_bytes = rfl::cbor::write(original);
    REQUIRE(!cbor_bytes.empty());

    auto deserialized = rfl::cbor::read<AllFavorites>(cbor_bytes);
    REQUIRE(deserialized);
    REQUIRE(deserialized->version == 1);
    REQUIRE(deserialized->lots.items.size() == 1);
    REQUIRE(deserialized->props.has_value());
    REQUIRE(deserialized->props->items.size() == 1);
    REQUIRE(deserialized->props->items[0].value() == 0x22222222);
    REQUIRE(deserialized->flora.has_value());
    REQUIRE(deserialized->flora->items.size() == 1);
    REQUIRE(deserialized->flora->items[0].value() == 0x33333333);
}

TEST_CASE("AllFavorites CBOR empty favorites", "[cbor][favorites][edge-case]") {
    AllFavorites original{
        .version = 1,
        .lots = {.items = {}},
        .props = std::nullopt,
        .flora = std::nullopt,
        .lastModified = rfl::Timestamp<"%Y-%m-%dT%H:%M:%S">::from_string("2026-01-20T00:00:00")
    };

    auto cbor_bytes = rfl::cbor::write(original);
    REQUIRE(!cbor_bytes.empty());

    auto deserialized = rfl::cbor::read<AllFavorites>(cbor_bytes);
    REQUIRE(deserialized);
    REQUIRE(deserialized->lots.items.empty());
    REQUIRE(!deserialized->props.has_value());
    REQUIRE(!deserialized->flora.has_value());
}
