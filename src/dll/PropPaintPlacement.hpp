#pragma once

#include <cstdint>

#include "cS3DVector3.h"

struct PlannedProp {
    cS3DVector3 position{};
    int32_t rotation = 0;
    uint32_t propID = 0;
};
