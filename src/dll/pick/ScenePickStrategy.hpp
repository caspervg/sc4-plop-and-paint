#pragma once

#include <optional>

#include "ScenePickResult.hpp"
#include "cISC4City.h"
#include "cS3DVector3.h"

struct ScenePickContext {
    cISC4City* city{nullptr};
    cS3DVector3 cursorWorld{};
};

class ScenePickStrategy {
public:
    virtual ~ScenePickStrategy() = default;

    [[nodiscard]] virtual ScenePickMode Mode() const = 0;
    [[nodiscard]] virtual float PickRadiusMeters() const = 0;
    [[nodiscard]] virtual std::optional<ScenePickResult> Pick(const ScenePickContext& context) = 0;
    virtual void SetHover(const std::optional<ScenePickResult>& result) = 0;
    virtual void ClearHover() = 0;

    // Overlapping candidates at the last Pick position; >1 enables wheel cycling.
    [[nodiscard]] virtual uint32_t CandidateCount() const { return 1; }
    // Index of the currently selected candidate within the stack.
    [[nodiscard]] virtual uint32_t CandidateIndex() const { return 0; }
    virtual void CycleCandidates(int32_t /*delta*/) {}
};
