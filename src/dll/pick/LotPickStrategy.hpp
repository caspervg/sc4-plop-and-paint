#pragma once

#include <optional>

#include "ScenePickStrategy.hpp"

class LotPickStrategy final : public ScenePickStrategy {
public:
    LotPickStrategy() = default;
    ~LotPickStrategy() override = default;

    [[nodiscard]] ScenePickMode Mode() const override;
    [[nodiscard]] float PickRadiusMeters() const override;
    [[nodiscard]] std::optional<ScenePickResult> Pick(const ScenePickContext& context) override;
    void SetHover(const std::optional<ScenePickResult>& result) override {}
    void ClearHover() override {}
};
