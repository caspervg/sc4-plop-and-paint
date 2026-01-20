#pragma once

#include <cstdint>
#include <string>
#include "rfl/Hex.hpp"

struct PropertyOption {
    rfl::Hex<uint32_t> value;
    std::string name;
};

struct PropertyDef {
    rfl::Hex<uint32_t> id;
    std::string name;
    std::vector<PropertyOption> options;  // Empty if no options
};

struct PropertiesData {
    std::vector<PropertyDef> properties;
};
