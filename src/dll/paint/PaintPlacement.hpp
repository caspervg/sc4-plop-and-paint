#pragma once

#include <cmath>
#include <cstdint>
#include <numbers>

#include "cS3DVector3.h"

struct PlannedPaint {
    cS3DVector3 position{};
    int32_t rotation = 0;
    uint32_t itemId = 0;
};

// The quarter turn that points an item along a direction.
inline int32_t QuantizeRotationStep(const float dirX, const float dirZ) {
    const float angle = std::atan2(dirX, dirZ);
    int32_t step = static_cast<int32_t>(std::lround(angle / (std::numbers::pi_v<float> * 0.5f)));
    step %= 4;
    if (step < 0) {
        step += 4;
    }
    return step;
}
