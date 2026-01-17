#pragma once
#include <__filesystem/filesystem_error.h>


#include "ExemplarReader.h"
#include "rfl/Attribute.hpp"
#include "rfl/Rename.hpp"

using BoolLiteral = rfl::Literal<"Y", "N">;

// XML structure for <OPTION Value="..." Name="..."/>
struct PropertyOption {
    rfl::Rename<"Value", rfl::Attribute<std::string>> value;
    rfl::Rename<"Label", rfl::Attribute<std::string>> label;
};

struct PropertyDefinition {
    rfl::Rename<"ID", rfl::Attribute<std::string>> id;
    rfl::Rename<"Name", rfl::Attribute<std::string>> name;
    rfl::Rename<"Type", rfl::Attribute<std::string>> type;

    rfl::Rename<"Count", rfl::Attribute<std::optional<std::string>>> count;
    rfl::Rename<"ShowAsHex", rfl::Attribute<BoolLiteral>> showAsHex;
    rfl::Rename<"Default", rfl::Attribute<std::optional<std::string>>> defaultValue;

    rfl::Rename<"HELP", std::optional<std::string>> helpText;
    rfl::Rename<"OPTION", std::vector<PropertyOption>> options;
};

struct ExemplarProperties {
    struct Properties {
        rfl::Rename<"PROPERTY", std::vector<PropertyDefinition>> definitions;
    };
    rfl::Rename<"PROPERTIES", Properties> properties;
};

// Property metadata from XML
struct PropertyInfo {
    uint32_t id;
    std::string name;
    Exemplar::ValueType type;  // Use DBPFKit's enum!
    int count = 1;  // Default 1, -1 for variable-length arrays
    std::unordered_map<std::string, uint32_t> optionNames_;
};

class PropertyMapper {
public:
    bool loadFromXml(const std::filesystem::path& xmlPath);

    [[nodiscard]] std::optional<PropertyInfo> propertyInfo(uint32_t propertyId) const;
    [[nodiscard]] std::optional<PropertyInfo> propertyInfo(const std::string& propertyName) const;
    [[nodiscard]] std::string_view propertyName(uint32_t propertyId) const;
    [[nodiscard]] std::optional<uint32_t> propertyId(const std::string& propertyName) const;
    [[nodiscard]] std::optional<uint32_t> propertyOptionId(const std::string& propertyName, const std::string& optionName) const;

private:
    template<typename T>
    [[nodiscard]] bool validateType_(Exemplar::ValueType expected) const {
        if constexpr (std::is_same_v<T, uint8_t>) return expected == Exemplar::ValueType::UInt8;
        if constexpr (std::is_same_v<T, uint16_t>) return expected == Exemplar::ValueType::UInt16;
        if constexpr (std::is_same_v<T, uint32_t>) return expected == Exemplar::ValueType::UInt32;
        if constexpr (std::is_same_v<T, int32_t>) return expected == Exemplar::ValueType::SInt32;
        if constexpr (std::is_same_v<T, int64_t>) return expected == Exemplar::ValueType::SInt64;
        if constexpr (std::is_same_v<T, float>) return expected == Exemplar::ValueType::Float32;
        if constexpr (std::is_same_v<T, bool>) return expected == Exemplar::ValueType::Bool;
        if constexpr (std::is_same_v<T, std::string>) return expected == Exemplar::ValueType::String;
        return false;
    }

    template<typename T>
    [[nodiscard]] const char* typeName_() const {
        if constexpr (std::is_same_v<T, uint8_t>) return "uint8_t";
        if constexpr (std::is_same_v<T, uint16_t>) return "uint16_t";
        if constexpr (std::is_same_v<T, uint32_t>) return "uint32_t";
        if constexpr (std::is_same_v<T, std::string>) return "string";
        return "unknown";
    }

    [[nodiscard]] static uint32_t parsePropertyId_(const std::string& idStr);
    [[nodiscard]] static Exemplar::ValueType parseValueType_(const std::string& typeStr);
    [[nodiscard]] static int parseCount_(const std::optional<std::string>& countStr);

private:
    std::unordered_map<uint32_t, PropertyInfo> properties_;
    std::unordered_map<std::string, uint32_t> propertyNames_;
};
