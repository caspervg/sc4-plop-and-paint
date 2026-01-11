#pragma once
#include <string>
#include <unordered_set>

#include "rfl/Bytestring.hpp"
#include "rfl/Hex.hpp"
#include "rfl/TaggedUnion.hpp"

struct PreRendered {
    rfl::Bytestring data;
    uint32_t width;
    uint32_t height;
};

struct Icon {
    rfl::Bytestring data;
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

    Thumbnail thumbnail;
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

    Building building;
};