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
};

struct Building {
    rfl::Hex<uint32_t> instanceId;
    rfl::Hex<uint32_t> groupId;

    std::string name;
    std::string description;

    std::unordered_set<uint32_t> occupantGroups;

    std::optional<Thumbnail> thumbnail;

    std::vector<Lot> lots;
};

struct PropTimeOfDay {
    float startHour;
    float endHour;
};

struct SimulatorDateStart {
    uint8_t month;
    uint8_t day;
};

struct Prop {
    rfl::Hex<uint32_t> groupId;
    rfl::Hex<uint32_t> instanceId;

    std::string exemplarName;
    std::string visibleName;

    float width;
    float height;
    float depth;
    float minX = 0.0f;
    float maxX = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;
    float minZ = 0.0f;
    float maxZ = 0.0f;
    std::vector<rfl::Hex<uint32_t>> familyIds;
    std::optional<bool> nighttimeStateChange;
    std::optional<PropTimeOfDay> timeOfDay;
    std::optional<SimulatorDateStart> simulatorDateStart;
    std::optional<uint32_t> simulatorDateDuration;
    std::optional<uint32_t> simulatorDateInterval;
    std::optional<uint8_t> randomChance;

    std::optional<Thumbnail> thumbnail;
};

struct PropFamilyInfo {
    rfl::Hex<uint32_t> familyId;
    std::string displayName;
};

struct PropsCache {
    uint32_t version = 4;
    std::vector<Prop> props;
    std::vector<PropFamilyInfo> propFamilies;
};

struct FamilyEntry {
    rfl::Hex<uint32_t> propID;
    float weight = 1.0f;
};

struct PropFamily {
    std::string name;
    std::vector<FamilyEntry> entries;
    float densityVariation = 0.0f;
    std::optional<rfl::Hex<uint64_t>> persistentId;
};

struct Flora {
    rfl::Hex<uint32_t> groupId;
    rfl::Hex<uint32_t> instanceId;

    std::string exemplarName;
    std::string visibleName;

    float width{0};
    float height{0};
    float depth{0};
    float minX{0};
    float maxX{0};
    float minY{0};
    float maxY{0};
    float minZ{0};
    float maxZ{0};

    std::vector<rfl::Hex<uint32_t>> familyIds;
    std::optional<rfl::Hex<uint32_t>> clusterNextType;

    std::optional<Thumbnail> thumbnail;
};

struct FloraCache {
    uint32_t version = 1;
    std::vector<Flora> floraItems;
    std::vector<PropFamilyInfo> floraFamilies;
};

struct TabFavorites {
    std::vector<rfl::Hex<uint64_t>> items;
};

struct RecentPaintEntryData {
    uint8_t sourceKind = 0;
    rfl::Hex<uint64_t> sourceId;
    uint8_t kind = 0;
    rfl::Hex<uint32_t> typeId;
    rfl::Hex<uint64_t> thumbnailKey;
    std::string name;
    std::vector<FamilyEntry> palette;
};

struct AllFavorites {
    uint32_t version = 4;
    TabFavorites lots;
    std::optional<TabFavorites> props;
    std::optional<TabFavorites> flora;
    std::optional<std::vector<PropFamily>> families;
    std::optional<std::vector<RecentPaintEntryData>> recentPaints;
    rfl::Timestamp<"%Y-%m-%dT%H:%M:%S"> lastModified;
};
