#pragma once

#include <cstdint>
#include <vector>

#include "ScenePickStrategy.hpp"
#include "cISC4Occupant.h"
#include "cISC4PropManager.h"
#include "cRZAutoRefCount.h"

class PropRepository;

class PropPickStrategy final : public ScenePickStrategy {
public:
    // `propRepository` may be null; it is only used to skip out-of-season
    // seasonal props (invisible in the scene) when picking.
    PropPickStrategy(uint32_t sourceFlags, const PropRepository* propRepository);
    ~PropPickStrategy() override;

    [[nodiscard]] ScenePickMode Mode() const override;
    [[nodiscard]] float PickRadiusMeters() const override;
    [[nodiscard]] std::optional<ScenePickResult> Pick(const ScenePickContext& context) override;
    void SetHover(const std::optional<ScenePickResult>& result) override;
    void ClearHover() override;

private:
    struct CollectedProp {
        cISC4Occupant* occupant{nullptr};
        PickedPropSource source{PickedPropSource::City};
        cS3DVector3 position{};
        uint32_t propType{0};
        int32_t orientation{0};
    };

    [[nodiscard]] bool HasSource_(PickedPropSource source) const;
    static bool TryGetCursorCell_(cISC4City* city, const cS3DVector3& cursorWorld, int& cellX, int& cellZ);
    void AppendCandidateProps_(std::vector<CollectedProp>& candidates,
                               cISC4City* city,
                               cISC4PropManager* propManager,
                               const cS3DVector3& cursorWorld,
                               PickedPropSource source) const;
    [[nodiscard]] std::optional<PickedProp> PickNearestProp_(const ScenePickContext& context) const;
    [[nodiscard]] bool IsOutOfSeason_(uint32_t propType, int dayOfYear) const;
    static cISC4Occupant* OccupantFromResult_(const ScenePickResult& result);

    uint32_t sourceFlags_{0};
    const PropRepository* propRepository_{nullptr};
    cRZAutoRefCount<cISC4Occupant> hoveredOccupant_{};
};
