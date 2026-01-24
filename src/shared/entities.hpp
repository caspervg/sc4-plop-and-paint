#pragma once
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "rfl/Bytestring.hpp"
#include "rfl/Hex.hpp"
#include "rfl/TaggedUnion.hpp"
#include "rfl/Timestamp.hpp"

struct PreRendered {
    rfl::Bytestring data;  // RGBA32 pixel data (width * height * 4 bytes)
    uint32_t width;
    uint32_t height;
};

struct Icon {
    rfl::Bytestring data;  // RGBA32 pixel data (width * height * 4 bytes)
    uint32_t width;
    uint32_t height;
};

using Thumbnail = rfl::TaggedUnion<"type", PreRendered, Icon>;

struct Building {
    rfl::Hex<uint32_t> instanceId;
    rfl::Hex<uint32_t> groupId;

    std::string name;
    std::string description;

    std::unordered_set<uint32_t> occupantGroups;

    std::optional<Thumbnail> thumbnail;
};

struct Lot {
    rfl::Hex<uint32_t> instanceId;
    rfl::Hex<uint32_t> groupId;

    std::string name;

    uint8_t sizeX;
    uint8_t sizeZ;

    uint16_t minCapacity;
    uint16_t maxCapacity;

    uint8_t growthStage;

    std::optional<uint8_t> zoneType;      // LotConfigPropertyZoneTypes (0x88edc793)
    std::optional<uint8_t> wealthType;    // LotConfigPropertyWealthTypes (0x88edc795)
    std::optional<uint8_t> purposeType;   // LotConfigPropertyPurposeTypes (0x88edc796)

    Building building;
};

struct TabFavorites {
    std::vector<rfl::Hex<uint32_t>> items;
};

struct AllFavorites {
    uint32_t version = 1;
    TabFavorites lots;
    std::optional<TabFavorites> props;  // Future: prop favorites
    std::optional<TabFavorites> flora;  // Future: flora favorites
    rfl::Timestamp<"%Y-%m-%dT%H:%M:%S"> lastModified;
};