#pragma once

#include "DBPFReader.h"
#include "ExemplarReader.h"
#include "PropertyMapper.hpp"
#include "DbpfIndexService.hpp"
#include "../shared/entities.hpp"
#include <array>
#include <memory>
#include <filesystem>
#include <optional>

namespace thumb {
    class ThumbnailRenderer;
}

constexpr auto kZero = 0x0000000u;
constexpr auto kExemplarType = "Exemplar Type";
constexpr auto kExemplarTypeBuilding = "Buildings";
constexpr auto kExemplarTypeLotConfig = "LotConfigurations";
constexpr auto kExemplarTypeProp = "Prop";
constexpr auto kExemplarName = "Exemplar Name";
constexpr auto kItemName = "Item Name";
constexpr auto kUserVisibleNameKey = "User Visible Name Key";
constexpr auto kItemDescriptionKey = "Item Description Key";
constexpr auto kItemDescription = "Item Description";
constexpr auto kExemplarId = "Exemplar ID";
constexpr auto kOccupantGroups = "OccupantGroups";
constexpr auto kLotConfigSize = "LotConfigPropertySize";
constexpr auto kLotConfigObject = "LotConfigPropertyLotObject";
constexpr auto kLotConfigZoneType = "LotConfigPropertyZoneTypes";
constexpr auto kLotConfigWealthType = "LotConfigPropertyWealthTypes";
constexpr auto kLotConfigPurposeType = "LotConfigPropertyPurposeTypes";
constexpr auto kPropertyLotObjectsStart = 0x88EDC900u;
constexpr auto kPropertyLotObjectsEnd = 0x88EDCFF0u;
constexpr auto kLotConfigObjectTypeBuilding = kZero;
constexpr auto kGrowthStage = "Growth Stage";
constexpr auto kCapacity = "Capacity Satisfied";
constexpr auto kIconResourceKey = "Icon Resource Key";
constexpr auto kItemIcon = "Item Icon";
constexpr auto kTypeIdPNG = 0x856DDBACu;
constexpr auto kTypeIdS3D = 0x5AD0E817u;
constexpr auto kTypeIdLText = 0x2026960Bu;
constexpr auto kLotIconGroup = 0x6A386D26u;
constexpr auto kBuildingPropFamily = "Building/prop Family";
constexpr auto kBuildingFamilyAlt = "Building/Prop Family";
constexpr auto kExemplarTypeFlora = "Flora";        // ExemplarType value 0x0F
constexpr auto kFloraWild = "Flora: Wild";           // 0x6a37ebb6
constexpr auto kFloraFamily = "kSC4FloraFamilyProperty"; // 0xa8f149c5
constexpr auto kFloraClusterType = "Flora: Cluster type"; // 0x2a0348ba
constexpr auto kRkt0PropertyId = 0x27812820u;
constexpr auto kRkt1PropertyId = 0x27812821u;
constexpr auto kRkt2PropertyId = 0x27812822u;
constexpr auto kRkt3PropertyId = 0x27812823u;
constexpr auto kRkt4PropertyId = 0x27812824u;
constexpr auto kRkt5PropertyId = 0x27812825u;
constexpr auto kOccupantSize = "Occupant Size";
constexpr auto kNighttimeStateChange = "Nighttime State Change";
constexpr auto kPropTimeOfDay = "Prop Time of Day";
constexpr auto kSimulatorDateStart = "Simulator Date Start";
constexpr auto kSimulatorDateDuration = "Simulator Date Duration";
constexpr auto kSimulatorDateInterval = "Simulator Date Interval";
constexpr auto kPropRandomChance = "Prop Random Chance";

// Lot object array indices (0-based, spec uses 1-based rep numbers)
constexpr auto kLotObjectIndexType = 0; // Rep 1: Object type (0 = building, 1 = prop, etc.)
constexpr auto kLotObjectIndexObjectID = 11; // Rep 12: ObjectID (0xABBBBCCC format)
constexpr auto kLotObjectIndexIID = 12; // Rep 13: IID (building exemplar) or Family ID (for growables)
constexpr auto kDefaultThumbnailSize = 44u;

enum class ExemplarType {
    Building, // Exemplar Type 0x02
    LotConfig, // Exemplar Type 0x10
    Prop,     // Exemplar Type 0x1E
    Flora,    // Exemplar Type 0x0F
};

struct ParsedBuildingExemplar {
    DBPF::Tgi tgi;
    std::string name;
    std::string description;
    std::vector<uint32_t> occupantGroups;
    std::vector<uint32_t> familyIds; // Building/prop Family values
    std::optional<DBPF::Tgi> iconTgi;
    std::optional<DBPF::Tgi> modelTgi;
};

struct ParsedLotConfigExemplar {
    DBPF::Tgi tgi;
    std::string name;
    std::pair<uint8_t, uint8_t> lotSize;
    uint32_t buildingInstanceId;
    uint32_t buildingFamilyId = 0; // Family ID if isFamilyReference is true
    bool isFamilyReference = false; // True if lot references a family instead of specific building
    std::optional<uint8_t> growthStage;
    std::optional<std::pair<uint8_t, uint8_t>> capacity; // (min, max)
    std::optional<uint8_t> zoneType; // LotConfigPropertyZoneTypes
    std::optional<uint8_t> wealthType; // LotConfigPropertyWealthTypes
    std::optional<uint8_t> purposeType; // LotConfigPropertyPurposeTypes
};

struct ParsedFloraExemplar {
    DBPF::Tgi tgi;
    std::string exemplarName;
    std::string visibleName;
    float width{-1.0f};
    float height{-1.0f};
    float depth{-1.0f};
    float minX{0.0f};
    float maxX{0.0f};
    float minY{0.0f};
    float maxY{0.0f};
    float minZ{0.0f};
    float maxZ{0.0f};
    bool hasModelBounds{false};
    std::vector<uint32_t> familyIds;
    std::optional<uint32_t> clusterNextType;
    std::optional<DBPF::Tgi> modelTgi;
};

struct ParsedPropExemplar {
    DBPF::Tgi tgi;
    std::string exemplarName;
    std::string visibleName;
    float width{-1.0};
    float height{-1.0};
    float depth{-1.0};
    float minX{0.0f};
    float maxX{0.0f};
    float minY{0.0f};
    float maxY{0.0f};
    float minZ{0.0f};
    float maxZ{0.0f};
    bool hasModelBounds{false};
    std::vector<uint32_t> familyIds;
    std::optional<bool> nighttimeStateChange;
    std::optional<PropTimeOfDay> timeOfDay;
    std::optional<SimulatorDateStart> simulatorDateStart;
    std::optional<uint32_t> simulatorDateDuration;
    std::optional<uint32_t> simulatorDateInterval;
    std::optional<uint8_t> randomChance;
    std::optional<DBPF::Tgi> modelTgi;
};

class ExemplarParser {
public:
    explicit ExemplarParser(const PropertyMapper& mapper,
                            const DbpfIndexService* indexService = nullptr,
                            bool renderThumbnails = false,
                            uint32_t thumbnailSize = kDefaultThumbnailSize);
    ~ExemplarParser();

    [[nodiscard]] std::optional<ExemplarType> getExemplarType(const Exemplar::Record& exemplar) const;
    [[nodiscard]] std::optional<ParsedBuildingExemplar> parseBuilding(const Exemplar::Record& exemplar,
                                                                      const DBPF::Tgi& tgi) const;
    [[nodiscard]] std::optional<ParsedLotConfigExemplar> parseLotConfig(
        const Exemplar::Record& exemplar,
        const DBPF::Tgi& tgi,
        const std::unordered_map<uint32_t, ParsedBuildingExemplar>& buildingMap,
        const std::unordered_map<uint32_t, std::vector<uint32_t>>& familyToBuildingsMap) const;
    [[nodiscard]] std::optional<ParsedPropExemplar> parseProp(const Exemplar::Record& exemplar,
                                                              const DBPF::Tgi& tgi) const;
    [[nodiscard]] std::optional<ParsedFloraExemplar> parseFlora(const Exemplar::Record& exemplar,
                                                                const DBPF::Tgi& tgi) const;
    [[nodiscard]] std::optional<PropFamilyInfo> parsePropFamilyFromCohort(const Exemplar::Record& cohort) const;

    // Conversion functions to canonical entities
    [[nodiscard]] Building buildingFromParsed(const ParsedBuildingExemplar& parsed) const;
    [[nodiscard]] Lot lotFromParsed(const ParsedLotConfigExemplar& parsed) const;
    [[nodiscard]] Prop propFromParsed(const ParsedPropExemplar& parsed) const;
    [[nodiscard]] Flora floraFromParsed(const ParsedFloraExemplar& parsed) const;

    // Cohort-aware property lookup - searches exemplar and parent cohorts recursively
    [[nodiscard]] const Exemplar::Property* findProperty(
        const Exemplar::Record& exemplar,
        uint32_t propertyId
    ) const;

private:
    // Helper to recursively find properties in exemplar and parent cohorts
    [[nodiscard]] const Exemplar::Property* findPropertyRecursive(
        const Exemplar::Record& exemplar,
        uint32_t propertyId,
        std::unordered_set<uint32_t>& visitedCohorts
    ) const;
    [[nodiscard]] std::string resolveLTextTags_(std::string_view text,
                                                const Exemplar::Record& exemplar) const;
    [[nodiscard]] std::optional<DBPF::Tgi> resolveModelTgi_(const Exemplar::Record& exemplar,
                                                            const DBPF::Tgi& exemplarTgi) const;
    [[nodiscard]] std::optional<std::array<float, 6>> loadModelBounds_(const DBPF::Tgi& modelTgi) const;
    static std::vector<std::byte> convertBgraToRgba_(const std::vector<std::byte>& pixels);

    const PropertyMapper& propertyMapper_;
    const DbpfIndexService* indexService_;
    std::unique_ptr<thumb::ThumbnailRenderer> thumbnailRenderer_;
    uint32_t thumbnailSize_;

    // Cached property IDs (resolved once at construction)
    std::optional<uint32_t> pidExemplarType_;
    std::optional<uint32_t> pidItemName_;
    std::optional<uint32_t> pidUserVisibleNameKey_;
    std::optional<uint32_t> pidExemplarName_;
    std::optional<uint32_t> pidItemDescriptionKey_;
    std::optional<uint32_t> pidItemDescription_;
    std::optional<uint32_t> pidOccupantGroups_;
    std::optional<uint32_t> pidBuildingPropFamily_;
    std::optional<uint32_t> pidItemIcon_;
    std::optional<uint32_t> pidLotConfigSize_;
    std::optional<uint32_t> pidGrowthStage_;
    std::optional<uint32_t> pidLotConfigZoneType_;
    std::optional<uint32_t> pidLotConfigWealthType_;
    std::optional<uint32_t> pidLotConfigPurposeType_;
    std::optional<uint32_t> pidOccupantSize_;
    std::optional<uint32_t> pidNighttimeStateChange_;
    std::optional<uint32_t> pidPropTimeOfDay_;
    std::optional<uint32_t> pidSimulatorDateStart_;
    std::optional<uint32_t> pidSimulatorDateDuration_;
    std::optional<uint32_t> pidSimulatorDateInterval_;
    std::optional<uint32_t> pidPropRandomChance_;

    std::optional<uint32_t> pidFloraWild_;
    std::optional<uint32_t> pidFloraFamily_;
    std::optional<uint32_t> pidFloraClusterType_;

    // Cached exemplar type option IDs
    std::optional<uint32_t> optBuilding_;
    std::optional<uint32_t> optLotConfig_;
    std::optional<uint32_t> optProp_;
    std::optional<uint32_t> optFlora_;
};
