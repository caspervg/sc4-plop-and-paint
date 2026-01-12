#include "PropertyMapper.hpp"

#include "rfl/xml/load.hpp"
#include "spdlog/spdlog.h"

bool PropertyMapper::LoadFromXml(const std::filesystem::path& xmlPath) {
    try {
        auto result = rfl::xml::load<ExemplarProperties>(xmlPath.string());

        if (!result) {
            spdlog::error("Failed to parse properties XML: {}", result.error().what());
        }

        const auto& root = result.value();
        for (const auto& propDef : root.properties().definitions()) {
            PropertyInfo info{ParsePropertyId_(propDef.id().get()), propDef.name().get(),
                              ParseValueType_(propDef.type().get()), ParseCount_(propDef.count().get())};
            auto propOptionList = propDef.options.get();
            std::unordered_map<std::string, uint32_t> propOptionMap{};
            if (!propOptionList.empty()) {
                for (const auto& option : propOptionList) {
                    uint32_t optionValue = ParsePropertyId_(option.value().get());
                    std::string optionLabel = option.label().get();
                    propOptionMap[optionLabel] = optionValue;
                }
                info.optionNames_ = propOptionMap;
            }

            properties_[info.id] = info;
            propertyNames_[info.name] = info.id;
        }

        spdlog::info("Loaded {} property definitions from XML", properties_.size());
        return true;
    }
    catch (const std::exception& e) {
        spdlog::error("Exception loading XML: {}", e.what());
        return false;
    }
}

std::optional<PropertyInfo> PropertyMapper::GetPropertyInfo(const uint32_t propertyId) const {
    if (const auto it = properties_.find(propertyId); it != properties_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<PropertyInfo> PropertyMapper::GetPropertyInfo(const std::string& propertyName) const {
    const auto propertyId = GetPropertyId(propertyName);
    if (!propertyId) {
        return std::nullopt;
    }
    return GetPropertyInfo(*propertyId);
}

std::string_view PropertyMapper::GetPropertyName(const uint32_t propertyId) const {
    if (const auto it = properties_.find(propertyId); it != properties_.end()) {
        return it->second.name;
    }
    return "Unknown";
}

std::optional<uint32_t> PropertyMapper::GetPropertyId(const std::string& propertyName) const {
    if (propertyNames_.contains(propertyName)) {
        return propertyNames_.at(propertyName);
    }
    return std::nullopt;
}

std::optional<uint32_t> PropertyMapper::GetPropertyOptionId(const std::string& propertyName, const std::string& optionName) const {
    const auto propertyId = GetPropertyId(propertyName);
    if (!propertyId) {
        return std::nullopt;
    }
    const auto propertyInfo = GetPropertyInfo(*propertyId).value();
    if (propertyInfo.optionNames_.contains(optionName)) {
        return propertyInfo.optionNames_.at(optionName);
    }
    return std::nullopt;
}

uint32_t PropertyMapper::ParsePropertyId_(const std::string& idStr) {
    if (idStr.starts_with("0x") || idStr.starts_with("0X")) {
        return std::stoul(idStr, nullptr, 16);
    }

    spdlog::warn("Skipping symbolic property ID: {}", idStr);
    return 0;
}

Exemplar::ValueType PropertyMapper::ParseValueType_(const std::string& typeStr) {
    if (typeStr == "Uint8")
        return Exemplar::ValueType::UInt8;
    if (typeStr == "Uint16")
        return Exemplar::ValueType::UInt16;
    if (typeStr == "Uint32")
        return Exemplar::ValueType::UInt32;
    if (typeStr == "Sint32")
        return Exemplar::ValueType::SInt32;
    if (typeStr == "Sint64")
        return Exemplar::ValueType::SInt64;
    if (typeStr == "Float32")
        return Exemplar::ValueType::Float32;
    if (typeStr == "Bool")
        return Exemplar::ValueType::Bool;
    if (typeStr == "String")
        return Exemplar::ValueType::String;
    // Default to UInt32 for unknown types
    return Exemplar::ValueType::UInt32;
}

int PropertyMapper::ParseCount_(const std::optional<std::string>& countStr) {
    if (!countStr)
        return 1;
    return std::stoi(countStr.value());
}
