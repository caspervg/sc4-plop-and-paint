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

    // More Building Styles exemplar data. Optional values preserve the
    // distinction between an absent property and an explicit empty/false one.
    std::optional<std::vector<uint32_t>> buildingStyles;
    std::optional<bool> buildingIsWallToWall;
    std::optional<uint8_t> purposeType; // Purpose (0x27812833)
    std::optional<uint32_t> exemplarCategory; // Exemplar Category (0x2C8F8746)
    std::optional<bool> buildingStylesPimxTemplateMarker;

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

struct SeasonalSet {
    std::string name;
    std::vector<rfl::Hex<uint32_t>> members;  // prop instance IDs; date windows live on the Prop
    uint8_t confidence = 0;  // 0 = stem match, 1 = fuzzy match, 2 = user-defined
    std::optional<rfl::Hex<uint64_t>> persistentId;  // set only for user-curated sets
};

struct PropsCache {
    uint32_t version = 5;
    std::vector<Prop> props;
    std::vector<PropFamilyInfo> propFamilies;
    std::vector<SeasonalSet> seasonalSets;
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

struct SavedDecalInfo {
    float baseSize = 16.0f;
    float rotationTurns = 0.0f;
    float aspectMultiplier = 1.0f;
    float uvScaleU = 1.0f;
    float uvScaleV = 1.0f;
    float uvOffset = 0.0f;
    float unknown8 = 0.0f;
};

struct SavedDecalColor {
    float x = 1.0f;
    float y = 1.0f;
    float z = 1.0f;
};

struct SavedDecalUvWindow {
    float u1 = 0.0f;
    float v1 = 0.0f;
    float u2 = 1.0f;
    float v2 = 1.0f;
    uint32_t mode = 0;
};

struct SavedDecalPreset {
    uint32_t overlayType = 2;
    SavedDecalInfo decalInfo{};
    float opacity = 1.0f;
    bool enabled = true;
    SavedDecalColor color{};
    uint8_t drawMode = 0;
    uint32_t flags = 0;
    bool hasUvWindow = false;
    SavedDecalUvWindow uvWindow{};
    int depthOffset = -1;
};

struct NamedDecalPreset {
    std::string name;
    SavedDecalPreset preset{};
    bool isDefault = false;
};

struct FavoriteDecalEntry {
    rfl::Hex<uint32_t> instanceId;
    std::vector<NamedDecalPreset> presets;
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
    uint32_t version = 7;
    TabFavorites lots;
    std::optional<TabFavorites> props;
    std::optional<TabFavorites> flora;
    std::optional<std::vector<FavoriteDecalEntry>> decals;
    std::optional<std::vector<PropFamily>> families;
    std::optional<std::vector<RecentPaintEntryData>> recentPaints;
    std::optional<std::vector<SeasonalSet>> seasonalSets;
    rfl::Timestamp<"%Y-%m-%dT%H:%M:%S"> lastModified;
};
