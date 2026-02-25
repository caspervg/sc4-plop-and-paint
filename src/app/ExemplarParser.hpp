#pragma once

#include "DBPFReader.h"
#include "ExemplarReader.h"
#include "PropertyMapper.hpp"
#include "DbpfIndexService.hpp"
#include "../shared/entities.hpp"
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
constexpr auto kBuildingFamily = "Building/prop Family";
constexpr auto kBuildingFamilyAlt = "Building/Prop Family";
constexpr auto kRkt0PropertyId = 0x27812820u;
constexpr auto kRkt1PropertyId = 0x27812821u;
constexpr auto kRkt2PropertyId = 0x27812822u;
constexpr auto kRkt3PropertyId = 0x27812823u;
constexpr auto kRkt4PropertyId = 0x27812824u;
constexpr auto kRkt5PropertyId = 0x27812825u;
constexpr auto kOccupantSize = "Occupant Size";

// Lot object array indices (0-based, spec uses 1-based rep numbers)
constexpr auto kLotObjectIndexType = 0; // Rep 1: Object type (0 = building, 1 = prop, etc.)
constexpr auto kLotObjectIndexObjectID = 11; // Rep 12: ObjectID (0xABBBBCCC format)
constexpr auto kLotObjectIndexIID = 12; // Rep 13: IID (building exemplar) or Family ID (for growables)
constexpr auto kRenderedThumbnailSize = 44u;

enum class ExemplarType {
    Building, // Exemplar Type 0x02
    LotConfig, // Exemplar Type 0x10
    Prop, // Exemplar Type 0x1E
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

struct ParsedPropExemplar {
    DBPF::Tgi tgi;
    std::string exemplarName;
    std::string visibleName;
    float width{-1.0};
    float height{-1.0};
    float depth{-1.0};
    std::vector<uint32_t> familyIds;
    std::optional<DBPF::Tgi> modelTgi;
};

class ExemplarParser {
public:
    explicit ExemplarParser(const PropertyMapper& mapper,
                            const DbpfIndexService* indexService = nullptr,
                            bool renderThumbnails = false);
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
    [[nodiscard]] std::optional<PropFamilyInfo> parsePropFamilyFromCohort(const Exemplar::Record& cohort) const;

    // Conversion functions to canonical entities
    [[nodiscard]] Building buildingFromParsed(const ParsedBuildingExemplar& parsed) const;
    [[nodiscard]] Lot lotFromParsed(const ParsedLotConfigExemplar& parsed) const;
    [[nodiscard]] Prop propFromParsed(const ParsedPropExemplar& parsed) const;

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
    static std::vector<std::byte> convertBgraToRgba_(const std::vector<std::byte>& pixels);

    const PropertyMapper& propertyMapper_;
    const DbpfIndexService* indexService_;
    std::unique_ptr<thumb::ThumbnailRenderer> thumbnailRenderer_;
};
