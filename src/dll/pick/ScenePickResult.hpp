#pragma once

#include <cstdint>
#include <variant>

#include "cISC4Occupant.h"
#include "cRZAutoRefCount.h"
#include "cS3DVector3.h"

enum class ScenePickMode : uint8_t {
    Prop = 0,
    Flora = 1,
};

enum class PickedPropSource : uint8_t {
    City = 0,
    Lot = 1,
    Street = 2,
};

struct PickedProp {
    cRZAutoRefCount<cISC4Occupant> occupant{};
    PickedPropSource source{PickedPropSource::City};
    cS3DVector3 position{};
    uint32_t propType{0};
    int32_t orientation{0};
};

struct PickedFlora {
    cRZAutoRefCount<cISC4Occupant> occupant{};
    cS3DVector3 position{};
    uint32_t floraType{0};
    int32_t orientation{0};
};

using ScenePickResult = std::variant<PickedProp, PickedFlora>;
