#include "ScenePickerInputControl.hpp"

#include <type_traits>
#include <variant>
#include <windows.h>

#include "cISTETerrain.h"
#include "../utils/Logger.h"

namespace {
    constexpr auto kScenePickerControlID = 0x7F6E5D4Du;
    constexpr DWORD kPickerNoTargetColor = 0xC0FF3333;
    constexpr DWORD kPickerTargetColor = 0xC033DD66;
    constexpr DWORD kPickerRectHighlightColor = 0xF0FFD700;
}

ScenePickerInputControl::ScenePickerInputControl()
    : cSC4BaseViewInputControl(kScenePickerControlID) {}

ScenePickerInputControl::~ScenePickerInputControl() = default;

bool ScenePickerInputControl::Init() {
    if (initialized) {
        return true;
    }
    if (!cSC4BaseViewInputControl::Init()) {
        return false;
    }
    LOG_INFO("ScenePickerInputControl initialized");
    return true;
}

bool ScenePickerInputControl::Shutdown() {
    if (!initialized) {
        return true;
    }
    ClearHover_();
    pickedResult_.reset();
    cSC4BaseViewInputControl::Shutdown();
    LOG_INFO("ScenePickerInputControl shut down");
    return true;
}

bool ScenePickerInputControl::OnMouseDownL(const int32_t /*x*/,
                                           const int32_t /*z*/,
                                           const uint32_t /*modifiers*/) {
    if (!active_ || !IsOnTop() || !cursorValid_ || !strategy_) {
        return false;
    }

    RefreshHover_();
    if (!hoveredResult_) {
        return false;
    }

    pickedResult_ = hoveredResult_;
    pickPending_ = true;
    ClearHover_();
    return true;
}

bool ScenePickerInputControl::OnMouseDownR(const int32_t /*x*/,
                                           const int32_t /*z*/,
                                           const uint32_t /*modifiers*/) {
    if (!active_ || !IsOnTop()) {
        return false;
    }
    ClearHover_();
    cancelPending_ = true;
    return true;
}

bool ScenePickerInputControl::OnMouseMove(const int32_t x, const int32_t z, const uint32_t /*modifiers*/) {
    if (!active_ || !IsOnTop()) {
        return false;
    }
    UpdateCursorWorldFromScreen_(x, z);
    RefreshHover_();
    BuildOverlay_();
    return true;
}

bool ScenePickerInputControl::OnMouseWheel(const int32_t x, const int32_t z, const uint32_t modifiers,
                                           const int32_t wheelDelta) {
    // Alt+scroll cycles the stack (matching other DLLs that consume the
    // wheel); plain scroll always stays camera zoom.
    const bool altHeld = (GetKeyState(VK_MENU) & 0x8000) != 0;
    if (altHeld && active_ && IsOnTop() && strategy_ && wheelDelta != 0 && strategy_->CandidateCount() > 1) {
        strategy_->CycleCandidates(wheelDelta > 0 ? 1 : -1);
        UpdateCursorWorldFromScreen_(x, z);
        RefreshHover_();
        BuildOverlay_();
        return true;
    }
    return cSC4BaseViewInputControl::OnMouseWheel(x, z, modifiers, wheelDelta);
}

bool ScenePickerInputControl::OnKeyDown(const int32_t vkCode, const uint32_t /*modifiers*/) {
    if (!active_ || !IsOnTop()) {
        return false;
    }
    if (vkCode == VK_ESCAPE) {
        ClearHover_();
        cancelPending_ = true;
        return true;
    }
    return false;
}

void ScenePickerInputControl::Activate() {
    cSC4BaseViewInputControl::Activate();
    if (!Init()) {
        LOG_WARN("ScenePickerInputControl: Init failed during Activate");
        return;
    }
    active_ = true;
    LOG_INFO("ScenePickerInputControl activated");
}

void ScenePickerInputControl::Deactivate() {
    active_ = false;
    ClearHover_();
    overlay_.Clear();
    cSC4BaseViewInputControl::Deactivate();
    LOG_INFO("ScenePickerInputControl deactivated");
}

void ScenePickerInputControl::SetCity(cISC4City* city) {
    city_ = city;
    if (!city_) {
        ClearHover_();
    }
}

void ScenePickerInputControl::SetStrategy(std::unique_ptr<ScenePickStrategy> strategy) {
    ClearHover_();
    strategy_ = std::move(strategy);
}

ScenePickMode ScenePickerInputControl::GetMode() const {
    return strategy_ ? strategy_->Mode() : ScenePickMode::Prop;
}

void ScenePickerInputControl::SetOnPick(std::function<void(const ScenePickResult&)> onPick) {
    onPick_ = std::move(onPick);
}

void ScenePickerInputControl::SetOnCancel(std::function<void()> onCancel) {
    onCancel_ = std::move(onCancel);
}

void ScenePickerInputControl::ProcessPendingActions() {
    if (pickPending_) {
        pickPending_ = false;
        if (pickedResult_ && onPick_) {
            onPick_(*pickedResult_);
        }
        pickedResult_.reset();
        if (onCancel_) {
            onCancel_();
        }
    }
    else if (cancelPending_) {
        cancelPending_ = false;
        if (onCancel_) {
            onCancel_();
        }
    }
}

void ScenePickerInputControl::DrawOverlay(IDirect3DDevice7* device) {
    if (!device) {
        return;
    }
    overlay_.Draw(device, false);
}

bool ScenePickerInputControl::UpdateCursorWorldFromScreen_(const int32_t screenX, const int32_t screenZ) {
    cursorValid_ = false;
    if (!view3D) {
        return false;
    }

    float worldCoords[3] = {0.0f, 0.0f, 0.0f};
    if (!view3D->PickTerrain(screenX, screenZ, worldCoords, false)) {
        return false;
    }

    currentCursorWorld_ = cS3DVector3(worldCoords[0], worldCoords[1], worldCoords[2]);
    cursorValid_ = true;
    return true;
}

void ScenePickerInputControl::RefreshHover_() {
    if (!strategy_ || !cursorValid_) {
        ClearHover_();
        return;
    }

    ScenePickContext context;
    context.city = city_;
    context.cursorWorld = currentCursorWorld_;
    hoveredResult_ = strategy_->Pick(context);
    strategy_->SetHover(hoveredResult_);
}

void ScenePickerInputControl::BuildOverlay_() {
    if (!cursorValid_ || !strategy_) {
        overlay_.Clear();
        return;
    }
    overlay_.BuildStripperPreview(
        true,
        currentCursorWorld_,
        strategy_->PickRadiusMeters(),
        GetTerrain_(),
        hoveredResult_.has_value() ? kPickerTargetColor : kPickerNoTargetColor);

    // Outline the hovered item's world footprint (lot textures, lots) so the
    // selection has visible feedback.
    if (hoveredResult_) {
        std::visit([this](const auto& picked) {
            using T = std::decay_t<decltype(picked)>;
            if constexpr (std::is_same_v<T, PickedDecal> || std::is_same_v<T, PickedLot>) {
                if (picked.hasWorldRect) {
                    overlay_.AddRectOutline(picked.worldMinX, picked.worldMinZ,
                                            picked.worldMaxX, picked.worldMaxZ,
                                            GetTerrain_(), kPickerRectHighlightColor);
                }
            }
        }, *hoveredResult_);
    }
}

void ScenePickerInputControl::ClearHover_() {
    if (strategy_) {
        strategy_->ClearHover();
    }
    hoveredResult_.reset();
}

cISTETerrain* ScenePickerInputControl::GetTerrain_() const {
    if (!city_) {
        return nullptr;
    }
    return city_->GetTerrain();
}
