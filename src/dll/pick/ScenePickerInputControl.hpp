#pragma once

#include <functional>
#include <memory>
#include <optional>

#include "../paint/PaintOverlay.hpp"
#include "ScenePickResult.hpp"
#include "ScenePickStrategy.hpp"
#include "cISC4City.h"
#include "cRZAutoRefCount.h"
#include "cS3DVector3.h"
#include "cSC4BaseViewInputControl.h"

class cISTETerrain;

class ScenePickerInputControl : public cSC4BaseViewInputControl {
public:
    ScenePickerInputControl();
    ~ScenePickerInputControl() override;

    bool Init() override;
    bool Shutdown() override;

    bool OnMouseDownL(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseDownR(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseMove(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseWheel(int32_t x, int32_t z, uint32_t modifiers, int32_t wheelDelta) override;
    bool OnKeyDown(int32_t vkCode, uint32_t modifiers) override;

    void Activate() override;
    void Deactivate() override;

    void SetCity(cISC4City* city);
    void SetStrategy(std::unique_ptr<ScenePickStrategy> strategy);
    [[nodiscard]] ScenePickMode GetMode() const;
    [[nodiscard]] bool IsActive() const { return active_; }
    [[nodiscard]] const std::optional<ScenePickResult>& GetHoveredResult() const { return hoveredResult_; }
    [[nodiscard]] uint32_t GetCandidateCount() const { return strategy_ ? strategy_->CandidateCount() : 0; }
    [[nodiscard]] uint32_t GetCandidateIndex() const { return strategy_ ? strategy_->CandidateIndex() : 0; }
    void SetOnPick(std::function<void(const ScenePickResult&)> onPick);
    void SetOnCancel(std::function<void()> onCancel);
    void ProcessPendingActions();
    void DrawOverlay(IDirect3DDevice7* device);

private:
    bool UpdateCursorWorldFromScreen_(int32_t screenX, int32_t screenZ);
    void RefreshHover_();
    void BuildOverlay_();
    void ClearHover_();
    [[nodiscard]] cISTETerrain* GetTerrain_() const;

    cRZAutoRefCount<cISC4City> city_{};
    std::unique_ptr<ScenePickStrategy> strategy_{};
    bool active_{false};
    bool cancelPending_{false};
    bool pickPending_{false};
    std::optional<ScenePickResult> hoveredResult_{};
    std::optional<ScenePickResult> pickedResult_{};
    cS3DVector3 currentCursorWorld_{};
    bool cursorValid_{false};
    std::function<void(const ScenePickResult&)> onPick_{};
    std::function<void()> onCancel_{};
    PaintOverlay overlay_{};
};
