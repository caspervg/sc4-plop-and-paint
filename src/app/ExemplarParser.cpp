#include "ExemplarParser.hpp"

std::optional<ExemplarType> ExemplarParser::GetExemplarType(const Exemplar::Record& exemplar) {
    auto* prop = exemplar.FindProperty(kPropertyExemplarType);
    if (!prop || prop->values.empty()) {
        // Unknown or empty Exemplar Type
        return std::nullopt;
    }

    const auto exemplarType = std::get<uint32_t>(prop->values.front());
    if (exemplarType == kPropertyExemplarTypeValueBuilding) return ExemplarType::Building;
    if (exemplarType == kPropertyExemplarTypeValueLotConfig) return ExemplarType::LotConfig;
    return std::nullopt;
}

std::optional<ParsedBuildingExemplar> ExemplarParser::ParseBuilding(const DBPF::Reader& reader, const DBPF::IndexEntry& entry) {
    auto exemplar = reader.LoadExemplar(entry);
    if (!exemplar) return std::nullopt;

    ParsedBuildingExemplar parsedBuildingExemplar;
    parsedBuildingExemplar.tgi = entry.tgi;

    if (auto* prop = exemplar->FindProperty(kPropertyExemplarName)) {
        if (prop->IsString() && !prop->values.empty()) {
            parsedBuildingExemplar.name = std::get<std::string>(prop->values.front());
        }
    }

    if (auto* prop = exemplar->FindProperty(kPropertyBuildingOccupantGroups)) {
        if (prop->IsUint32Array()) {
            for (const auto& val : prop->values) {
                const auto& arr = std::get<std::vector<uint32_t>>(val);
                parsedBuildingExemplar.occupantGroups.insert(
                    parsedBuildingExemplar.occupantGroups.end(),
                    arr.begin(),
                    arr.end()
                );
            }
        }
    }

    return parsedBuildingExemplar;
}

std::optional<ParsedLotConfigExemplar> ExemplarParser::ParseLotConfig(const DBPF::Reader& reader, const DBPF::IndexEntry& entry) {
    auto exemplar = reader.LoadExemplar(entry);
    if (!exemplar) return std::nullopt;

    ParsedLotConfigExemplar parsedLotConfigExemplar;
    parsedLotConfigExemplar.tgi = entry.tgi;



    return parsedLotConfigExemplar;
}