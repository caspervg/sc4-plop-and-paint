#pragma once
#include <array>
#include <cstdint>
#include <optional>

#include "cGZPersistResourceKey.h"

// The models of a prop, as its exemplar's resource key properties name them.
// See https://wiki.sc4evermore.com/resource-key-type
struct PropModel {
    // True 3D models turn with the prop. The others have a model for every view, made for that
    // camera rotation, which the game turns with the camera instead.
    bool perView = false;
    uint32_t type = 0;
    uint32_t group = 0;
    std::array<uint32_t, 20> instances{}; // [zoom * 4 + rotation]

    // The model for a zoom (0-4) and a rotation slot (0-3).
    [[nodiscard]] cGZPersistResourceKey Key(int32_t zoom, int32_t rotation) const {
        return {type, group, instances[static_cast<size_t>(zoom * 4 + rotation)]};
    }
};

// Reads RKT0 to RKT5. For RKT4 only the first model of state 0 (or 1) is used, without its offset.
std::optional<PropModel> FindPropModel(uint32_t exemplarGroup, uint32_t exemplarInstance);
