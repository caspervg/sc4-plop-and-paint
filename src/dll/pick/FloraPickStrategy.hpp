#pragma once

#include "ScenePickStrategy.hpp"
#include "cISC4Occupant.h"
#include "cRZAutoRefCount.h"

class FloraPickStrategy final : public ScenePickStrategy {
public:
    FloraPickStrategy();
    ~FloraPickStrategy() override;

    [[nodiscard]] ScenePickMode Mode() const override;
    [[nodiscard]] float PickRadiusMeters() const override;
    [[nodiscard]] std::optional<ScenePickResult> Pick(const ScenePickContext& context) override;
    void SetHover(const std::optional<ScenePickResult>& result) override;
    void ClearHover() override;

private:
    [[nodiscard]] std::optional<PickedFlora> PickNearestFlora_(const ScenePickContext& context) const;
    static cISC4Occupant* OccupantFromResult_(const ScenePickResult& result);

    cRZAutoRefCount<cISC4Occupant> hoveredOccupant_{};
};
