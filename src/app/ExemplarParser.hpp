#pragma once

#include "DBPFReader.h"
#include "ExemplarReader.h"
#include "PropertyMapper.hpp"

constexpr auto kZero = 0x0000000u;
constexpr auto kExemplarType = "Exemplar Type";
constexpr auto kExemplarTypeBuilding = "Buildings";
constexpr auto kExemplarTypeLotConfig = "LotConfigurations";
constexpr auto kExemplarName = "Exemplar Name";
constexpr auto kExemplarId = "Exemplar ID";
constexpr auto kOccupantGroups = "OccupantGroups";
constexpr auto kLotConfigSize = "LotConfigPropertySize";
constexpr auto kLotConfigObject = "LotConfigPropertyLotObject";
constexpr auto kLotConfigObjectTypeBuilding = kZero;
constexpr auto kGrowthStage = "Growth Stage";
constexpr auto kCapacity = "Capacity Satisfied";
constexpr auto kIconResourceKey = "Icon Resource Key";
constexpr auto kItemIcon = "Item Icon";
constexpr auto kTypeIdPNG = 0x856DDBACu;

enum class ExemplarType {
    Building,   // Exemplar Type 0x02
    LotConfig,  // Exemplar Type 0x10
};

struct ParsedBuildingExemplar {
    DBPF::Tgi tgi;
    std::string name;
    std::vector<uint32_t> occupantGroups;
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
    explicit ExemplarParser(const PropertyMapper& mapper);

    [[nodiscard]] std::optional<ExemplarType> getExemplarType(const Exemplar::Record& exemplar) const;
    [[nodiscard]] std::optional<ParsedBuildingExemplar> parseBuilding(const DBPF::Reader& reader, const DBPF::IndexEntry& entry) const;
    [[nodiscard]] std::optional<ParsedLotConfigExemplar> parseLotConfig(const DBPF::Reader& reader, const DBPF::IndexEntry& entry) const;

private:
    const PropertyMapper& propertyMapper_;
};
