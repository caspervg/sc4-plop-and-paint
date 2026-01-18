#include "ExemplarParser.hpp"

#include <unordered_set>
#include <cstring>

#include "spdlog/spdlog.h"

namespace {
    // Parse PNG dimensions from the header (first 24 bytes)
    std::pair<uint32_t, uint32_t> parsePngDimensions(const std::vector<uint8_t>& data) {
        if (data.size() < 24) return {0, 0};
        // Check PNG signature: 89 50 4E 47 0D 0A 1A 0A
        if (data[0] != 0x89 || data[1] != 0x50 || data[2] != 0x4E || data[3] != 0x47) {
            return {0, 0};
        }
        // Width and height are at bytes 16-19 and 20-23 (big-endian)
        uint32_t width = (static_cast<uint32_t>(data[16]) << 24) |
                         (static_cast<uint32_t>(data[17]) << 16) |
                         (static_cast<uint32_t>(data[18]) << 8) |
                         static_cast<uint32_t>(data[19]);
        uint32_t height = (static_cast<uint32_t>(data[20]) << 24) |
                          (static_cast<uint32_t>(data[21]) << 16) |
                          (static_cast<uint32_t>(data[22]) << 8) |
                          static_cast<uint32_t>(data[23]);
        return {width, height};
    }
}

ExemplarParser::ExemplarParser(const PropertyMapper& mapper, const DbpfIndexService* indexService)
    : propertyMapper_(mapper), indexService_(indexService) {}

std::optional<ExemplarType> ExemplarParser::getExemplarType(const Exemplar::Record& exemplar) const {
    auto propIdOpt = propertyMapper_.propertyId(kExemplarType);
    if (!propIdOpt) {
        return std::nullopt;
    }

    // Use cohort-aware property lookup
    const auto* prop = findProperty(exemplar, *propIdOpt);
    if (!prop || prop->values.empty()) {
        // Unknown or empty Exemplar Type
        return std::nullopt;
    }

    const auto exemplarType = std::get<uint32_t>(prop->values.front());
    auto buildingTypeOpt = propertyMapper_.propertyOptionId(kExemplarType, kExemplarTypeBuilding);
    auto lotConfigTypeOpt = propertyMapper_.propertyOptionId(kExemplarType, kExemplarTypeLotConfig);

    if (buildingTypeOpt && exemplarType == *buildingTypeOpt) return ExemplarType::Building;
    if (lotConfigTypeOpt && exemplarType == *lotConfigTypeOpt) return ExemplarType::LotConfig;
    return std::nullopt;
}

std::optional<ParsedBuildingExemplar> ExemplarParser::parseBuilding(const Exemplar::Record& exemplar, const DBPF::Tgi& tgi) const {
    ParsedBuildingExemplar parsedBuildingExemplar;
    parsedBuildingExemplar.tgi = tgi;

    // Use cohort-aware property lookup for all properties
    if (auto* prop = findProperty(exemplar, propertyMapper_.propertyId(kExemplarName).value())) {
        if (prop->IsString() && !prop->values.empty()) {
            parsedBuildingExemplar.name = std::get<std::string>(prop->values.front());
        }
    }

    if (auto* prop = findProperty(exemplar, propertyMapper_.propertyId(kOccupantGroups).value())) {
        if (prop->IsNumericList()) {
            for (const auto& val : prop->values) {
                parsedBuildingExemplar.occupantGroups.push_back(std::get<uint32_t>(val));
            }
        }
    }

    if (auto* prop = findProperty(exemplar, propertyMapper_.propertyId(kItemIcon).value())) {
        if (!prop->values.empty()) {
            const auto iconInstance = std::get<uint32_t>(prop->values.front());
            parsedBuildingExemplar.iconTgi = DBPF::Tgi{
                kTypeIdPNG,
                kLotIconGroup,
                iconInstance
            };
        }
    }

    return parsedBuildingExemplar;
}

std::optional<ParsedLotConfigExemplar> ExemplarParser::parseLotConfig(const Exemplar::Record& exemplar, const DBPF::Tgi& tgi, const std::unordered_map<uint32_t, ParsedBuildingExemplar>& buildingMap) const {
    ParsedLotConfigExemplar parsedLotConfigExemplar;
    parsedLotConfigExemplar.tgi = tgi;
    parsedLotConfigExemplar.buildingInstanceId = 0;

    // Use cohort-aware property lookup for all properties
    if (auto* prop = findProperty(exemplar, propertyMapper_.propertyId(kExemplarName).value())) {
        if (prop->IsString() && !prop->values.empty()) {
            parsedLotConfigExemplar.name = std::get<std::string>(prop->values.front());
        }
    }

    if (auto* prop = findProperty(exemplar, propertyMapper_.propertyId(kLotConfigSize).value())) {
        if (prop->IsNumericList() && prop->values.size() >= 2) {
            auto width = std::get<uint8_t>(prop->values[0]);
            auto height = std::get<uint8_t>(prop->values[1]);
            parsedLotConfigExemplar.lotSize = {width, height};
        }
    }

    // Scan through the lot objects property ID range to find the building
    // Use cohort-aware lookup for lot object properties too
    for (uint32_t propID = kPropertyLotObjectsStart;
         propID <= kPropertyLotObjectsEnd;
         propID++) {

        if (auto* prop = findProperty(exemplar, propID)) {
            if (prop->values.size() >= 13) {
                const auto objectType = std::get<uint32_t>(prop->values[0]);
                if (objectType == kLotConfigObjectTypeBuilding) {
                    parsedLotConfigExemplar.buildingInstanceId = std::get<uint32_t>(prop->values[12]);
                    break;
                }
            }
        }
    }

    if (!parsedLotConfigExemplar.buildingInstanceId) {
        return std::nullopt;
    }

    if (auto* prop = findProperty(exemplar, propertyMapper_.propertyId(kGrowthStage).value())) {
        if (!prop->values.empty()) {
            parsedLotConfigExemplar.growthStage = std::get<uint8_t>(prop->values.front());
        }
    }

    // Look up the icon from the building exemplar using the building map
    if (parsedLotConfigExemplar.buildingInstanceId != 0) {
        auto it = buildingMap.find(parsedLotConfigExemplar.buildingInstanceId);
        if (it != buildingMap.end()) {
            const auto& building = it->second;
            if (building.iconTgi.has_value()) {
                parsedLotConfigExemplar.iconTgi = building.iconTgi;
            }
        }
    }

    return parsedLotConfigExemplar;
}

Building ExemplarParser::buildingFromParsed(const ParsedBuildingExemplar& parsed) const {
    Building building;
    building.instanceId = parsed.tgi.instance;
    building.groupId = parsed.tgi.group;
    building.name = parsed.name;
    building.description = "";  // Not available from exemplar data
    building.occupantGroups = std::unordered_set(parsed.occupantGroups.begin(), parsed.occupantGroups.end());
    building.thumbnail = std::nullopt;

    // Load icon if TGI is available and we have index service
    if (parsed.iconTgi.has_value() && indexService_) {
        auto pngData = indexService_->loadEntryData(*parsed.iconTgi);
        if (pngData.has_value() && !pngData->empty()) {
            auto [width, height] = parsePngDimensions(*pngData);

            // Convert uint8_t vector to std::byte vector for rfl::Bytestring
            std::vector<std::byte> byteVec(pngData->size());
            std::memcpy(byteVec.data(), pngData->data(), pngData->size());

            Icon icon;
            icon.data = rfl::Bytestring(std::move(byteVec));
            icon.width = width;
            icon.height = height;
            building.thumbnail = icon;
        }
    }

    return building;
}

Lot ExemplarParser::lotFromParsed(const ParsedLotConfigExemplar& parsed, const Building& building) const {
    Lot lot;
    lot.instanceId = parsed.tgi.instance;
    lot.groupId = parsed.tgi.group;
    lot.name = parsed.name;
    lot.sizeX = parsed.lotSize.first;
    lot.sizeZ = parsed.lotSize.second;
    lot.minCapacity = parsed.capacity.has_value() ? parsed.capacity->first : 0;
    lot.maxCapacity = parsed.capacity.has_value() ? parsed.capacity->second : 0;
    lot.growthStage = parsed.growthStage.has_value() ? parsed.growthStage.value() : 0;
    lot.building = building;
    return lot;
}

const Exemplar::Property* ExemplarParser::findProperty(
    const Exemplar::Record& exemplar,
    uint32_t propertyId
) const {
    // If we have an index service, use recursive cohort following
    if (indexService_) {
        std::unordered_set<uint32_t> visitedCohorts;
        return findPropertyRecursive(exemplar, propertyId, visitedCohorts);
    }
    // Otherwise, just direct lookup
    return exemplar.FindProperty(propertyId);
}

const Exemplar::Property* ExemplarParser::findPropertyRecursive(
    const Exemplar::Record& exemplar,
    const uint32_t propertyId,
    std::unordered_set<uint32_t>& visitedCohorts
) const {
    // Check the current exemplar for the property
    if (auto* prop = exemplar.FindProperty(propertyId)) {
        return prop;
    }

    // If no index service, can't look up parent cohorts across files
    if (!indexService_) {
        return nullptr;
    }

    // Parent cohort is stored in the exemplar header, not as a property
    // Check if this exemplar has a parent cohort (instance != 0)
    if (exemplar.parent.instance == 0) {
        return nullptr;
    }

    // Prevent infinite loops
    if (visitedCohorts.contains(exemplar.parent.instance)) {
        return nullptr;
    }
    visitedCohorts.insert(exemplar.parent.instance);

    // Use the index service to find the parent cohort across all DBPF files
    const auto& tgiIndex = indexService_->tgiIndex();

    // Use the full parent TGI (type, group, instance) from the exemplar header
    const DBPF::Tgi& parentTgi = exemplar.parent;

    auto tgiIt = tgiIndex.find(parentTgi);

    if (tgiIt != tgiIndex.end()) {
        // Use the index service's cached loader instead of opening files repeatedly
        auto parentExemplar = indexService_->loadExemplar(parentTgi);
        if (parentExemplar.has_value()) {
            // Recursively search parent
            spdlog::info("Searching parent cohort 0x{:08X}/0x{:08X}/0x{:08X} for property 0x{:08X}", parentTgi.type, parentTgi.group, parentTgi.instance, propertyId);
            const auto prop = findPropertyRecursive(*parentExemplar, propertyId, visitedCohorts);
            if (prop != nullptr) {
                spdlog::info("Found property 0x{:08X} in parent cohort: {}", propertyId, prop->ToString());
                return prop;
            } else {
                spdlog::info("Property 0x{:08X} not found in parent cohort", propertyId);
            }
        } else {
            // Failed to load parent - log for debugging
            spdlog::warn("Failed to load parent cohort 0x{:08X}/0x{:08X}/0x{:08X}: {}",
                         parentTgi.type, parentTgi.group, parentTgi.instance, parentExemplar.error().message);
        }
    }

    return nullptr;
}