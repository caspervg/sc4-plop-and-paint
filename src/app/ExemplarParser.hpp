#pragma once

#include "DBPFReader.h"
#include "ExemplarReader.h"
#include "PropertyMapper.hpp"
#include "services/DbpfIndexService.hpp"
#include "../shared/entities.hpp"

constexpr auto kZero = 0x0000000u;
constexpr auto kExemplarType = "Exemplar Type";
constexpr auto kExemplarTypeBuilding = "Buildings";
constexpr auto kExemplarTypeLotConfig = "LotConfigurations";
constexpr auto kExemplarName = "Exemplar Name";
constexpr auto kExemplarId = "Exemplar ID";
constexpr auto kOccupantGroups = "OccupantGroups";
constexpr auto kLotConfigSize = "LotConfigPropertySize";
constexpr auto kLotConfigObject = "LotConfigPropertyLotObject";
constexpr auto kPropertyLotObjectsStart = 0x88EDC900u;
constexpr auto kPropertyLotObjectsEnd = 0x88EDCFF0u;
constexpr auto kLotConfigObjectTypeBuilding = kZero;
constexpr auto kGrowthStage = "Growth Stage";
constexpr auto kCapacity = "Capacity Satisfied";
constexpr auto kIconResourceKey = "Icon Resource Key";
constexpr auto kItemIcon = "Item Icon";
constexpr auto kTypeIdPNG = 0x856DDBACu;
constexpr auto kLotIconGroup = 0x6A386D26u;

enum class ExemplarType {
    Building,   // Exemplar Type 0x02
    LotConfig,  // Exemplar Type 0x10
};

struct ParsedBuildingExemplar {
    DBPF::Tgi tgi;
    std::string name;
    std::vector<uint32_t> occupantGroups;
    std::optional<DBPF::Tgi> iconTgi;
    // TODO other building props
};

struct ParsedLotConfigExemplar {
    DBPF::Tgi tgi;
    std::string name;
    std::pair<uint8_t, uint8_t> lotSize;
    uint32_t buildingInstanceId;
    std::optional<uint8_t> growthStage;
    std::optional<std::pair<uint8_t, uint8_t>> capacity; // (min, max)
    std::optional<DBPF::Tgi> iconTgi;
};

class ExemplarParser {
public:
    explicit ExemplarParser(const PropertyMapper& mapper, const DbpfIndexService* indexService = nullptr);

    [[nodiscard]] std::optional<ExemplarType> getExemplarType(const Exemplar::Record& exemplar) const;
    [[nodiscard]] std::optional<ParsedBuildingExemplar> parseBuilding(const Exemplar::Record& exemplar, const DBPF::Tgi& tgi) const;
    [[nodiscard]] std::optional<ParsedLotConfigExemplar> parseLotConfig(const Exemplar::Record& exemplar, const DBPF::Tgi& tgi, const std::unordered_map<uint32_t, ParsedBuildingExemplar>& buildingMap) const;

    // Conversion functions to canonical entities
    [[nodiscard]] Building buildingFromParsed(const ParsedBuildingExemplar& parsed) const;
    [[nodiscard]] Lot lotFromParsed(const ParsedLotConfigExemplar& parsed, const Building& building) const;

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

    const PropertyMapper& propertyMapper_;
    const DbpfIndexService* indexService_;
};
