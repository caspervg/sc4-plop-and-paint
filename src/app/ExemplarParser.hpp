#pragma once

#include "DBPFReader.h"
#include "ExemplarReader.h"
#include "PropertyMapper.hpp"

constexpr auto kExemplarType = "Exemplar Type";
constexpr auto kPropertyExemplarTypeValueBuilding = 0x02;
constexpr auto kPropertyExemplarTypeValueLotConfig = 0x10;
constexpr auto kPropertyExemplarName = 0x00000020;
constexpr auto kPropertyBuildingOccupantGroups = 0xAA1DD396;

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

    std::optional<ExemplarType> GetExemplarType(const Exemplar::Record& exemplar);

    std::optional<ParsedBuildingExemplar> ParseBuilding(const DBPF::Reader& reader, const DBPF::IndexEntry& entry);

    std::optional<ParsedLotConfigExemplar> ParseLotConfig(const DBPF::Reader& reader, const DBPF::IndexEntry& entry);

private:
    const PropertyMapper& propertyMapper_;
};
