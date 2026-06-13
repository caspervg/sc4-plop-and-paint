#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "entities.hpp"

struct RecentPaintEntry {
    enum class Kind : uint8_t { Prop = 0, Flora = 1 };

    Kind kind{Kind::Prop};
    uint32_t instanceId{0};
    uint64_t thumbnailKey{0};
    std::string name;
    std::vector<FamilyEntry> palette;
};