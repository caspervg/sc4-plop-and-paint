#include "Utils.hpp"

#include <format>
#include <iterator>
#include <string_view>

#include <utf8cpp/utf8.h>

#include "spdlog/spdlog.h"

namespace {
    bool SanitizeField(std::string& value, std::string_view fieldName, auto&&... ids) {
        if (utf8::is_valid(value.begin(), value.end())) {
            return false;
        }

        std::string idInfo;
        (void(std::initializer_list<int>{(idInfo += std::format(" 0x{:08X}", ids), 0)...}));

        spdlog::warn("Invalid UTF-8 in {}{}: '{}'. Sanitizing before serialization", fieldName, idInfo, value);
        value = SanitizeString(value);
        return true;
    }
}

size_t SanitizeStrings(std::vector<Building>& allBuildings, std::vector<Prop>& allProps) {
    size_t sanitizedFields = 0;
    const auto sanitizeAndCount = [&](std::string& value, std::string_view fieldName, auto&&... ids) {
        sanitizedFields += static_cast<size_t>(SanitizeField(value, fieldName, ids...));
    };

    for (auto& building : allBuildings) {
        sanitizeAndCount(building.name, "building.name", building.groupId.value(), building.instanceId.value());
        sanitizeAndCount(building.description, "building.description", building.groupId.value(),
                         building.instanceId.value());

        for (auto& lot : building.lots) {
            sanitizeAndCount(lot.name, "lot.name", lot.groupId.value(), lot.instanceId.value());
        }
    }

    for (auto& prop : allProps) {
        sanitizeAndCount(prop.exemplarName, "prop.exemplarName", prop.groupId.value(), prop.instanceId.value());
        sanitizeAndCount(prop.visibleName, "prop.visibleName", prop.groupId.value(), prop.instanceId.value());
    }

    if (sanitizedFields > 0) {
        spdlog::warn("Sanitized {} invalid UTF-8 fields before writing output", sanitizedFields);
    }

    return sanitizedFields;
}

std::string SanitizeString(const std::string_view text) {
    if (utf8::is_valid(text.begin(), text.end())) {
        return std::string(text);
    }

    std::string sanitized;
    sanitized.reserve(text.size());
    utf8::replace_invalid(text.begin(), text.end(), std::back_inserter(sanitized));
    return sanitized;
}
