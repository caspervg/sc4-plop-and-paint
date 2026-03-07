#pragma once

#include <cstdint>

#include "cS3DVector3.h"

struct PlannedPaint {
    cS3DVector3 position{};
    int32_t rotation = 0;
    uint32_t itemId = 0;
};
