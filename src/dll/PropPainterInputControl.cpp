#include "PropPainterInputControl.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <windows.h>

#include "PropLinePlacer.hpp"
#include "PropPolygonPlacer.hpp"
#include "PropRepository.hpp"
#include "WeightedPropPicker.hpp"
#include "cISC4Occupant.h"
#include "cISTETerrain.h"
#include "utils/Logger.h"

namespace {
    constexpr auto kPropPainterControlID = 0x8A3F9D2B;
    constexpr size_t kMaxPreviewPlacements = 5000;

    void PopulatePreviewBounds(PropPaintOverlay::PreviewPlacement& previewPlacement, const Prop& prop) {
        previewPlacement.width = prop.width;
        previewPlacement.height = prop.height;
        previewPlacement.depth = prop.depth;
        previewPlacement.minX = prop.minX;
        previewPlacement.maxX = prop.maxX;
        previewPlacement.minY = prop.minY;
        previewPlacement.maxY = prop.maxY;
        previewPlacement.minZ = prop.minZ;
        previewPlacement.maxZ = prop.maxZ;
    }

    float SnapCoordinate(const float value, const float gridStep) {
        if (gridStep <= 0.0f) {
            return value;
        }
        return std::round(value / gridStep) * gridStep;
    }

    float ClampDeltaY(const float value) {
        return std::max(value, 0.0f);
    }
}

PropPainterInputControl::PropPainterInputControl()
    : cSC4BaseViewInputControl(kPropPainterControlID)
      , propIDToPaint_(0)
      , settings_({}) {}

PropPainterInputControl::~PropPainterInputControl() = default;

bool PropPainterInputControl::Init() {
    if (state_ != ControlState::Uninitialized) {
        return true;
    }

    if (!cSC4BaseViewInputControl::Init()) {
        return false;
    }

    TransitionTo_(propIDToPaint_ != 0 ? ControlState::ReadyWithTarget : ControlState::ReadyNoTarget, "Init");
    LOG_INFO("PropPainterInputControl initialized");
    return true;
}

bool PropPainterInputControl::Shutdown() {
    if (state_ == ControlState::Uninitialized) {
        return true;
    }

    LOG_INFO("PropPainterInputControl shutting down");
    CancelAllPlacements();
    TransitionTo_(ControlState::Uninitialized, "Shutdown");
    cSC4BaseViewInputControl::Shutdown();
    return true;
}

bool PropPainterInputControl::OnMouseDownL(const int32_t x, const int32_t z, const uint32_t modifiers) {
    if (!IsActiveState_(state_) || !IsOnTop()) {
        return false;
    }

    return HandleActiveMouseDownL_(x, z, modifiers);
}

bool PropPainterInputControl::OnMouseDownR(const int32_t /*x*/, const int32_t /*z*/, const uint32_t /*modifiers*/) {
    if (!IsActiveState_(state_) || !IsOnTop()) {
        return false;
    }
    CancelAllPlacements();
    LOG_INFO("PropPainterInputControl: RMB pressed, stopping paint mode");
    cancelPending_ = true;
    return true;
}

bool PropPainterInputControl::OnMouseMove(const int32_t x, const int32_t z, const uint32_t modifiers) {
    if (!IsActiveState_(state_) || !IsOnTop()) {
        return false;
    }

    return HandleActiveMouseMove_(x, z, modifiers);
}

bool PropPainterInputControl::OnKeyDown(const int32_t vkCode, const uint32_t modifiers) {
    if (!IsActiveState_(state_) || !IsOnTop()) {
        return false;
    }

    return HandleActiveKeyDown_(vkCode, modifiers);
}

void PropPainterInputControl::Activate() {
    cSC4BaseViewInputControl::Activate();
    if (!Init()) {
        LOG_WARN("PropPainterInputControl: Init failed during Activate");
        return;
    }

    TransitionTo_(propIDToPaint_ != 0 ? ActiveStateForMode_(settings_.mode) : ControlState::ActiveNoTarget, "Activate");
    LOG_INFO("PropPainterInputControl activated");
}

void PropPainterInputControl::Deactivate() {
    ClearCollectedPoints_();
    DestroyPreviewProp_();

    if (state_ != ControlState::Uninitialized) {
        TransitionTo_(propIDToPaint_ != 0 ? ControlState::ReadyWithTarget : ControlState::ReadyNoTarget,
                      "Deactivate");
    }

    cSC4BaseViewInputControl::Deactivate();
    LOG_INFO("PropPainterInputControl deactivated");
}

void PropPainterInputControl::SetPropToPaint(const uint32_t propID, const PropPaintSettings& settings,
                                             const std::string& name) {
    const bool targetChanged = propIDToPaint_ != propID;

    propIDToPaint_ = propID;
    settings_ = settings;
    settings_.deltaYMeters = ClampDeltaY(settings_.deltaYMeters);
    settings_.densityVariation = std::clamp(settings_.densityVariation, 0.0f, 1.0f);
    if (settings_.randomSeed == 0) {
        settings_.randomSeed = static_cast<uint32_t>(GetTickCount64() ^ static_cast<uint64_t>(propIDToPaint_));
    }
    ResetDirectPaintPicker_();
    LOG_INFO("Setting prop to paint: {} (0x{:08X}), rotation: {}", name, propID, settings.rotation);

    if (targetChanged) {
        ClearCollectedPoints_();
        DestroyPreviewProp_();
    }
    cachedPolygonPlacements_.clear();
    polygonPreviewDirty_ = true;

    if (state_ == ControlState::Uninitialized) {
        return;
    }

    if (propIDToPaint_ == 0) {
        TransitionTo_(IsActiveState_(state_) ? ControlState::ActiveNoTarget : ControlState::ReadyNoTarget,
                      "SetPropToPaint clear target");
        return;
    }

    TransitionTo_(IsActiveState_(state_) ? ActiveStateForMode_(settings_.mode) : ControlState::ReadyWithTarget,
                  "SetPropToPaint");
}

void PropPainterInputControl::SetCity(cISC4City* pCity) {
    city_ = pCity;
    if (pCity) {
        propManager_ = pCity->GetPropManager();
    }
    else {
        DestroyPreviewProp_();
        propManager_.Reset();
        ClearCollectedPoints_();
    }
}

void PropPainterInputControl::SetCameraService(cIGZS3DCameraService* cameraService) {
    cameraService_ = cameraService;
}

void PropPainterInputControl::SetPropRepository(const PropRepository* propRepository) {
    propRepository_ = propRepository;
}

void PropPainterInputControl::SetOnCancel(std::function<void()> onCancel) {
    onCancel_ = std::move(onCancel);
}

void PropPainterInputControl::ProcessPendingActions() {
    if (cancelPending_) {
        cancelPending_ = false;
        if (onCancel_) {
            onCancel_();
        }
    }
}

void PropPainterInputControl::DrawOverlay(IDirect3DDevice7* device) {
    if (!device || settings_.previewMode == PropPreviewMode::Hidden) {
        return;
    }

    if (state_ == ControlState::ActiveDirect ||
        state_ == ControlState::ActiveLine ||
        state_ == ControlState::ActivePolygon) {
        overlay_.Draw(device);
    }
}

bool PropPainterInputControl::IsActiveState_(const ControlState state) {
    return state == ControlState::ActiveNoTarget ||
        state == ControlState::ActiveDirect ||
        state == ControlState::ActiveLine ||
        state == ControlState::ActivePolygon;
}

bool PropPainterInputControl::IsTargetActiveState_(const ControlState state) {
    return state == ControlState::ActiveDirect ||
        state == ControlState::ActiveLine ||
        state == ControlState::ActivePolygon;
}

PropPainterInputControl::ControlState PropPainterInputControl::ActiveStateForMode_(const PropPaintMode mode) {
    switch (mode) {
    case PropPaintMode::Direct:
        return ControlState::ActiveDirect;
    case PropPaintMode::Line:
        return ControlState::ActiveLine;
    case PropPaintMode::Polygon:
        return ControlState::ActivePolygon;
    default:
        return ControlState::ActiveDirect;
    }
}

const char* PropPainterInputControl::StateToString_(const ControlState state) {
    switch (state) {
    case ControlState::Uninitialized:
        return "Uninitialized";
    case ControlState::ReadyNoTarget:
        return "ReadyNoTarget";
    case ControlState::ReadyWithTarget:
        return "ReadyWithTarget";
    case ControlState::ActiveNoTarget:
        return "ActiveNoTarget";
    case ControlState::ActiveDirect:
        return "ActiveDirect";
    case ControlState::ActiveLine:
        return "ActiveLine";
    case ControlState::ActivePolygon:
        return "ActivePolygon";
    default:
        return "Unknown";
    }
}

void PropPainterInputControl::TransitionTo_(const ControlState newState, const char* reason) {
    if (state_ == newState) {
        SyncPreviewForState_();
        return;
    }

    const auto oldState = state_;
    state_ = newState;
    LOG_INFO("PropPainterInputControl state transition: {} -> {} ({})",
              StateToString_(oldState), StateToString_(newState), reason);
    SyncPreviewForState_();
}

void PropPainterInputControl::SyncPreviewForState_() {
    if (ShouldShowModelPreview_()) {
        if (previewProp_ && previewPropID_ != CurrentDirectPropID_()) {
            DestroyPreviewProp_();
        }
        if (!previewProp_) {
            CreatePreviewProp_();
        }
        UpdatePreviewPropRotation_();
        UpdatePreviewProp_();
    }
    else {
        DestroyPreviewProp_();
    }

    if (!ShouldShowOutlinePreview_()) {
        overlay_.Clear();
    }
    else {
        RebuildPreviewOverlay_();
    }
}

bool PropPainterInputControl::HandleActiveMouseDownL_(const int32_t x, const int32_t z, uint32_t /*modifiers*/) {
    switch (state_) {
    case ControlState::ActiveDirect:
        return PlacePropAt_(x, z);
    case ControlState::ActiveLine:
    case ControlState::ActivePolygon:
        if (!UpdateCursorWorldFromScreen_(x, z)) {
            return false;
        }
        collectedPoints_.push_back({currentCursorWorld_});
        if (state_ == ControlState::ActivePolygon) {
            polygonPreviewDirty_ = true;
        }
        RebuildPreviewOverlay_();
        return true;
    case ControlState::ActiveNoTarget:
    default:
        return false;
    }
}

bool PropPainterInputControl::HandleActiveMouseMove_(const int32_t x, const int32_t z, uint32_t /*modifiers*/) {
    if (!IsTargetActiveState_(state_)) {
        return false;
    }

    UpdateCursorWorldFromScreen_(x, z);
    SyncPreviewForState_();

    return true;
}

bool PropPainterInputControl::HandleActiveKeyDown_(const int32_t vkCode, const uint32_t modifiers) {
    if (vkCode == VK_ESCAPE) {
        CancelAllPlacements();
        LOG_INFO("PropPainterInputControl: ESC pressed, stopping paint mode");
        cancelPending_ = true;
        return true;
    }

    if (!IsTargetActiveState_(state_)) {
        return false;
    }

    if (vkCode == 'R') {
        settings_.rotation = (settings_.rotation + 1) & 3;
        if (state_ == ControlState::ActivePolygon) {
            polygonPreviewDirty_ = true;
        }
        SyncPreviewForState_();
        return true;
    }

    if (vkCode == VK_BACK && (modifiers & MOD_CONTROL)) {
        UndoLastPlacementInGroup();
        return true;
    }

    if (vkCode == 'Z' && (modifiers & MOD_CONTROL)) {
        UndoLastPlacement();
        return true;
    }

    if (vkCode == VK_BACK && (state_ == ControlState::ActiveLine || state_ == ControlState::ActivePolygon)) {
        if (!collectedPoints_.empty()) {
            collectedPoints_.pop_back();
            if (state_ == ControlState::ActivePolygon) {
                polygonPreviewDirty_ = true;
            }
            RebuildPreviewOverlay_();
            return true;
        }
    }

    if (vkCode == VK_RETURN) {
        if (state_ == ControlState::ActiveLine && collectedPoints_.size() >= 2) {
            ExecuteLinePlacement_();
            return true;
        }
        if (state_ == ControlState::ActivePolygon && collectedPoints_.size() >= 3) {
            ExecutePolygonPlacement_();
            return true;
        }
        CommitPlacements();
        return true;
    }

    if (vkCode == 'P') {
        int mode = (static_cast<int>(settings_.previewMode) + 1) % 4;
        settings_.previewMode = static_cast<PropPreviewMode>(mode);
        static constexpr const char* kNames[] = {"Outline", "Full", "Combined", "Hidden"};
        LOG_INFO("Preview mode: {}", kNames[mode]);
        SyncPreviewForState_();
        return true;
    }

    if (vkCode == 'G') {
        settings_.showGrid = !settings_.showGrid;
        LOG_INFO("Toggled grid visibility: {}", settings_.showGrid);
        SyncPreviewForState_();
        return true;
    }

    if (vkCode == 'S') {
        settings_.snapPointsToGrid = !settings_.snapPointsToGrid;
        if (!settings_.snapPointsToGrid) {
            settings_.snapPlacementsToGrid = false;
        }
        LOG_INFO("Toggled snap to grid: {}", settings_.snapPointsToGrid);
        SyncPreviewForState_();
        return true;
    }

    if (vkCode == VK_OEM_4 || vkCode == VK_OEM_6) { // [ and ]
        static constexpr float kGridSteps[] = {1.0f, 2.0f, 4.0f, 8.0f, 16.0f};
        static constexpr int kGridStepCount = sizeof(kGridSteps) / sizeof(kGridSteps[0]);

        int currentIdx = 0;
        float minDist = 999.0f;
        for (int i = 0; i < kGridStepCount; ++i) {
            const float dist = std::abs(settings_.gridStepMeters - kGridSteps[i]);
            if (dist < minDist) {
                minDist = dist;
                currentIdx = i;
            }
        }

        if (vkCode == VK_OEM_4 && currentIdx > 0) {
            --currentIdx;
        } else if (vkCode == VK_OEM_6 && currentIdx < kGridStepCount - 1) {
            ++currentIdx;
        }

        settings_.gridStepMeters = kGridSteps[currentIdx];
        LOG_INFO("Grid step: {:.1f}m", settings_.gridStepMeters);
        SyncPreviewForState_();
        return true;
    }

    if (vkCode == VK_OEM_MINUS || vkCode == VK_OEM_PLUS) {
        const float lineDelta = (vkCode == VK_OEM_PLUS) ? 0.5f : -0.5f;
        const float densityDelta = (vkCode == VK_OEM_PLUS) ? 0.25f : -0.25f;
        const bool controlHeld = (modifiers & MOD_CONTROL) != 0;

        if (state_ == ControlState::ActiveLine) {
            settings_.spacingMeters = std::clamp(settings_.spacingMeters + lineDelta, 0.5f, 50.0f);
            LOG_INFO("Line spacing: {:.1f}m", settings_.spacingMeters);
        } else if (state_ == ControlState::ActivePolygon) {
            if (controlHeld) {
                const float variationDelta = (vkCode == VK_OEM_PLUS) ? 0.05f : -0.05f;
                settings_.densityVariation = std::clamp(settings_.densityVariation + variationDelta, 0.0f, 1.0f);
                LOG_INFO("Polygon density variation: {:.2f}", settings_.densityVariation);
            }
            else {
                settings_.densityPer100Sqm = std::clamp(settings_.densityPer100Sqm + densityDelta, 0.1f, 10.0f);
                LOG_INFO("Polygon density: {:.2f}/100m^2", settings_.densityPer100Sqm);
            }
            polygonPreviewDirty_ = true;
        } else {
            return false;
        }

        SyncPreviewForState_();
        return true;
    }

    return false;
}

bool PropPainterInputControl::UpdateCursorWorldFromScreen_(const int32_t screenX, const int32_t screenZ) {
    const bool hadValidCursor = cursorValid_;
    const cS3DVector3 previousCursorWorld = currentCursorWorld_;
    cursorValid_ = false;
    if (!view3D) {
        return false;
    }

    const bool hidePreviewForPick =
        state_ == ControlState::ActiveDirect && previewActive_ && previewOccupant_;
    if (hidePreviewForPick) {
        previewOccupant_->SetVisibility(false, true);
    }

    float worldCoords[3] = {0.0f, 0.0f, 0.0f};
    if (!view3D->PickTerrain(screenX, screenZ, worldCoords, false)) {
        if (hidePreviewForPick && hadValidCursor) {
            currentCursorWorld_ = previousCursorWorld;
            cursorValid_ = true;
            return true;
        }
        return false;
    }

    currentCursorWorld_ = cS3DVector3(worldCoords[0], worldCoords[1], worldCoords[2]);
    if (settings_.snapPointsToGrid) {
        currentCursorWorld_ = SnapWorldToGrid_(currentCursorWorld_);
    }
    cursorValid_ = true;
    return true;
}

float PropPainterInputControl::GetGridStepMeters_() const {
    return std::max(settings_.gridStepMeters, 1.0f);
}

cS3DVector3 PropPainterInputControl::SnapWorldToGrid_(const cS3DVector3& position) const {
    const float gridStep = GetGridStepMeters_();
    return {
        SnapCoordinate(position.fX, gridStep),
        position.fY,
        SnapCoordinate(position.fZ, gridStep)};
}

void PropPainterInputControl::SnapPlacementToGrid_(PlannedProp& placement) const {
    if (!settings_.snapPlacementsToGrid) {
        return;
    }

    placement.position = SnapWorldToGrid_(placement.position);
}

void PropPainterInputControl::ClearCollectedPoints_() {
    collectedPoints_.clear();
    cursorValid_ = false;
    cachedPolygonPlacements_.clear();
    polygonPreviewDirty_ = true;
    overlay_.Clear();
}

void PropPainterInputControl::ResetDirectPaintPicker_() {
    directPaintPicker_.reset();
    directPaintPropID_ = propIDToPaint_;

    if (settings_.activePalette.empty()) {
        return;
    }

    directPaintPicker_ = std::make_unique<WeightedPropPicker>(settings_.activePalette, settings_.randomSeed);
    directPaintPropID_ = directPaintPicker_->Pick();
    if (directPaintPropID_ == 0) {
        directPaintPropID_ = propIDToPaint_;
    }
}

uint32_t PropPainterInputControl::CurrentDirectPropID_() const {
    return directPaintPropID_ != 0 ? directPaintPropID_ : propIDToPaint_;
}

void PropPainterInputControl::AdvanceDirectPaintProp_() {
    if (!directPaintPicker_) {
        directPaintPropID_ = propIDToPaint_;
        return;
    }

    directPaintPropID_ = directPaintPicker_->Pick();
    if (directPaintPropID_ == 0) {
        directPaintPropID_ = propIDToPaint_;
    }
}

void PropPainterInputControl::RebuildPreviewOverlay_() {
    if (!ShouldShowOutlinePreview_()) {
        overlay_.Clear();
        return;
    }

    if (state_ == ControlState::ActiveDirect) {
        const bool usePreviewAnchor = ShouldShowModelPreview_() && previewPositionValid_;
        const bool hasOverlayCursor = cursorValid_ || usePreviewAnchor;
        cS3DVector3 overlayCursor = currentCursorWorld_;
        if (!cursorValid_ && usePreviewAnchor) {
            overlayCursor = cS3DVector3(lastPreviewPosition_.fX, lastPreviewPosition_.fY, lastPreviewPosition_.fZ);
        }

        PropPaintOverlay::PreviewPlacement previewPlacement;
        previewPlacement.placement.position = overlayCursor;
        previewPlacement.placement.position.fY += settings_.deltaYMeters;
        previewPlacement.placement.rotation = settings_.rotation;
        previewPlacement.placement.propID = CurrentDirectPropID_();

        if (hasOverlayCursor && propRepository_) {
            if (const Prop* prop = propRepository_->FindPropByInstanceId(previewPlacement.placement.propID)) {
                PopulatePreviewBounds(previewPlacement, *prop);
            }
        }

        const bool drawPlacement = settings_.previewMode != PropPreviewMode::FullModel;
        overlay_.BuildDirectPreview(hasOverlayCursor, overlayCursor, GetTerrain_(), settings_, previewPlacement,
                                    drawPlacement);
        return;
    }

    std::vector<cS3DVector3> points;
    points.reserve(collectedPoints_.size());
    for (const auto& cp : collectedPoints_) {
        points.push_back(cp.worldPos);
    }

    std::vector<cS3DVector3> previewPoints = points;
    if (cursorValid_) {
        previewPoints.push_back(currentCursorWorld_);
    }

    std::unique_ptr<WeightedPropPicker> picker;
    uint32_t singlePropID = propIDToPaint_;
    if (!settings_.activePalette.empty()) {
        picker = std::make_unique<WeightedPropPicker>(settings_.activePalette, settings_.randomSeed);
        singlePropID = 0;
    }

    if (state_ == ControlState::ActiveLine) {
        std::vector<PropPaintOverlay::PreviewPlacement> plannedPlacements;
        plannedPlacements.reserve(kMaxPreviewPlacements);

        if (previewPoints.size() >= 2) {
            const auto placements = PropLinePlacer::ComputePlacements(
                previewPoints,
                std::max(settings_.spacingMeters, 0.25f),
                settings_.rotation,
                settings_.alignToPath,
                settings_.randomRotation,
                settings_.randomOffset,
                GetTerrain_(),
                settings_.randomSeed,
                picker.get(),
                singlePropID,
                kMaxPreviewPlacements);

            for (const auto& placement : placements) {
                PropPaintOverlay::PreviewPlacement previewPlacement;
                previewPlacement.placement = placement;
                previewPlacement.placement.position.fY += settings_.deltaYMeters;
                SnapPlacementToGrid_(previewPlacement.placement);

                if (propRepository_) {
                    if (const Prop* prop = propRepository_->FindPropByInstanceId(previewPlacement.placement.propID)) {
                        PopulatePreviewBounds(previewPlacement, *prop);
                    }
                }

                plannedPlacements.push_back(previewPlacement);
            }
        }

        overlay_.BuildLinePreview(points, currentCursorWorld_, cursorValid_, GetTerrain_(), settings_, plannedPlacements);
        return;
    }

    if (points.size() >= 3 && polygonPreviewDirty_) {
        cachedPolygonPlacements_.clear();
        cachedPolygonPlacements_.reserve(kMaxPreviewPlacements);

        const auto placements = PropPolygonPlacer::ComputePlacements(
            points,
            std::max(settings_.densityPer100Sqm, 0.1f),
            settings_.densityVariation,
            settings_.rotation,
            settings_.randomRotation,
            GetTerrain_(),
            settings_.randomSeed,
            picker.get(),
            singlePropID,
            kMaxPreviewPlacements);

        for (const auto& placement : placements) {
            PropPaintOverlay::PreviewPlacement previewPlacement;
            previewPlacement.placement = placement;
            previewPlacement.placement.position.fY += settings_.deltaYMeters;
            SnapPlacementToGrid_(previewPlacement.placement);

            if (propRepository_) {
                if (const Prop* prop = propRepository_->FindPropByInstanceId(previewPlacement.placement.propID)) {
                    PopulatePreviewBounds(previewPlacement, *prop);
                }
            }

            cachedPolygonPlacements_.push_back(previewPlacement);
        }

        polygonPreviewDirty_ = false;
    }

    overlay_.BuildPolygonPreview(points, currentCursorWorld_, cursorValid_, GetTerrain_(), settings_, cachedPolygonPlacements_);
}

void PropPainterInputControl::ExecuteLinePlacement_() {
    if (collectedPoints_.size() < 2) {
        return;
    }

    std::vector<cS3DVector3> linePoints;
    linePoints.reserve(collectedPoints_.size());
    for (const auto& cp : collectedPoints_) {
        linePoints.push_back(cp.worldPos);
    }

    std::unique_ptr<WeightedPropPicker> picker;
    uint32_t singlePropID = propIDToPaint_;
    if (!settings_.activePalette.empty()) {
        picker = std::make_unique<WeightedPropPicker>(settings_.activePalette, settings_.randomSeed);
        singlePropID = 0;
    }

    const auto placements = PropLinePlacer::ComputePlacements(
        linePoints,
        std::max(settings_.spacingMeters, 0.25f),
        settings_.rotation,
        settings_.alignToPath,
        settings_.randomRotation,
        settings_.randomOffset,
        GetTerrain_(),
        settings_.randomSeed,
        picker.get(),
        singlePropID);

    batchingPlacements_ = true;
    currentUndoGroup_.props.clear();
    size_t placedCount = 0;
    for (auto placement : placements) {
        placement.position.fY += settings_.deltaYMeters;
        SnapPlacementToGrid_(placement);
        if (PlacePropAtWorld_(placement.position, placement.rotation, placement.propID)) {
            ++placedCount;
        }
    }
    batchingPlacements_ = false;
    if (!currentUndoGroup_.props.empty()) {
        undoStack_.push_back(std::move(currentUndoGroup_));
    }
    currentUndoGroup_.props.clear();

    LOG_INFO("Line paint executed: placed {} / {} props", placedCount, placements.size());
    ClearCollectedPoints_();
}

void PropPainterInputControl::ExecutePolygonPlacement_() {
    if (collectedPoints_.size() < 3) {
        return;
    }

    std::vector<cS3DVector3> polygonVertices;
    polygonVertices.reserve(collectedPoints_.size());
    for (const auto& cp : collectedPoints_) {
        polygonVertices.push_back(cp.worldPos);
    }

    std::unique_ptr<WeightedPropPicker> picker;
    uint32_t singlePropID = propIDToPaint_;
    if (!settings_.activePalette.empty()) {
        picker = std::make_unique<WeightedPropPicker>(settings_.activePalette, settings_.randomSeed);
        singlePropID = 0;
    }

    const auto placements = PropPolygonPlacer::ComputePlacements(
        polygonVertices,
        std::max(settings_.densityPer100Sqm, 0.1f),
        settings_.densityVariation,
        settings_.rotation,
        settings_.randomRotation,
        GetTerrain_(),
        settings_.randomSeed,
        picker.get(),
        singlePropID);

    batchingPlacements_ = true;
    currentUndoGroup_.props.clear();
    size_t placedCount = 0;
    for (auto placement : placements) {
        placement.position.fY += settings_.deltaYMeters;
        SnapPlacementToGrid_(placement);
        if (PlacePropAtWorld_(placement.position, placement.rotation, placement.propID)) {
            ++placedCount;
        }
    }
    batchingPlacements_ = false;
    if (!currentUndoGroup_.props.empty()) {
        undoStack_.push_back(std::move(currentUndoGroup_));
    }
    currentUndoGroup_.props.clear();

    LOG_INFO("Polygon paint executed: placed {} / {} props", placedCount, placements.size());
    ClearCollectedPoints_();
}

void PropPainterInputControl::UndoLastPlacement() {
    if (undoStack_.empty()) {
        LOG_DEBUG("No props to undo");
        return;
    }

    if (!propManager_) {
        LOG_WARN("No prop manager available during undo; clearing local placed prop history");
        undoStack_.clear();
        currentUndoGroup_.props.clear();
        batchingPlacements_ = false;
        return;
    }

    UndoGroup group = std::move(undoStack_.back());
    undoStack_.pop_back();

    size_t removedCount = 0;
    for (auto it = group.props.rbegin(); it != group.props.rend(); ++it) {
        if (propManager_->RemovePropA(*it)) {
            ++removedCount;
        }
        else {
            LOG_WARN("Failed to remove placed prop during undo");
        }
    }

    LOG_INFO("Undid placement group: removed {} prop(s), {} remaining", removedCount, PendingPlacementCount_());
}

void PropPainterInputControl::UndoLastPlacementInGroup() {
    if (undoStack_.empty()) {
        LOG_DEBUG("No props to undo");
        return;
    }

    if (!propManager_) {
        LOG_WARN("No prop manager available during undo; clearing local placed prop history");
        undoStack_.clear();
        currentUndoGroup_.props.clear();
        batchingPlacements_ = false;
        return;
    }

    UndoGroup& group = undoStack_.back();
    if (group.props.empty()) {
        undoStack_.pop_back();
        LOG_DEBUG("Dropped empty undo group");
        return;
    }

    const auto& lastProp = group.props.back();
    if (propManager_->RemovePropA(lastProp)) {
        LOG_INFO("Undid last prop in group ({} remaining)", PendingPlacementCount_() - 1);
    }
    else {
        LOG_WARN("Failed to remove placed prop during per-prop undo");
    }

    group.props.pop_back();
    if (group.props.empty()) {
        undoStack_.pop_back();
    }
}

void PropPainterInputControl::CancelAllPlacements() {
    const size_t pendingCount = PendingPlacementCount_();
    if (pendingCount > 0) {
        if (!propManager_) {
            LOG_WARN("No prop manager available during cancel; clearing local placed prop history");
            undoStack_.clear();
        }
        else {
            LOG_INFO("Canceling {} placed props", pendingCount);

            for (auto groupIt = undoStack_.rbegin(); groupIt != undoStack_.rend(); ++groupIt) {
                for (auto propIt = groupIt->props.rbegin(); propIt != groupIt->props.rend(); ++propIt) {
                    if (propManager_->RemovePropA(*propIt)) {
                        LOG_DEBUG("Removed placed prop");
                    }
                    else {
                        LOG_WARN("Failed to remove placed prop");
                    }
                }
            }

            undoStack_.clear();
        }
    }

    batchingPlacements_ = false;
    currentUndoGroup_.props.clear();
    ClearCollectedPoints_();
}

void PropPainterInputControl::CommitPlacements() {
    const size_t pendingCount = PendingPlacementCount_();
    LOG_INFO("Committing {} placed props", pendingCount);
    for (const auto& group : undoStack_) {
        for (const auto& prop : group.props) {
            prop->SetHighlight(0x0, true);
        }
    }
    undoStack_.clear();
    batchingPlacements_ = false;
    currentUndoGroup_.props.clear();
}

bool PropPainterInputControl::PlacePropAt_(const int32_t screenX, const int32_t screenZ) {
    if (!view3D) {
        LOG_WARN("PropPainterInputControl: View3D not available");
        return false;
    }

    float worldCoords[3] = {0.0f, 0.0f, 0.0f};
    if (!view3D->PickTerrain(screenX, screenZ, worldCoords, false)) {
        LOG_DEBUG("Failed to pick terrain at screen ({}, {})", screenX, screenZ);
        return false;
    }

    cS3DVector3 targetPosition(worldCoords[0], worldCoords[1], worldCoords[2]);
    if (settings_.snapPointsToGrid) {
        targetPosition = SnapWorldToGrid_(targetPosition);
    }
    targetPosition.fY += settings_.deltaYMeters;

    const bool placed = PlacePropAtWorld_(targetPosition, settings_.rotation, CurrentDirectPropID_());
    if (placed) {
        AdvanceDirectPaintProp_();
        SyncPreviewForState_();
    }
    return placed;
}

bool PropPainterInputControl::PlacePropAtWorld_(const cS3DVector3& position, const int32_t rotation,
                                                const uint32_t propID) {
    if (!propManager_) {
        LOG_WARN("PropPainterInputControl: PropManager not available");
        return false;
    }

    const uint32_t propToCreate = propID != 0 ? propID : propIDToPaint_;
    if (propToCreate == 0) {
        LOG_WARN("PlacePropAtWorld_: no prop ID available");
        return false;
    }

    cISC4PropOccupant* prop = nullptr;
    if (!propManager_->CreateProp(propToCreate, prop)) {
        LOG_WARN("Failed to create prop 0x{:08X}", propToCreate);
        return false;
    }

    cRZAutoRefCount propRef(prop);
    cISC4Occupant* occupant = prop->AsOccupant();
    if (!occupant) {
        LOG_WARN("Failed to get occupant interface from created prop");
        return false;
    }

    cS3DVector3 placePos = position;
    if (!occupant->SetPosition(&placePos)) {
        LOG_WARN("Failed to set prop position");
        return false;
    }

    if (!prop->SetOrientation(rotation & 3)) {
        LOG_WARN("Failed to set prop orientation");
        return false;
    }

    if (!propManager_->AddCityProp(occupant)) {
        LOG_WARN("Failed to add prop to city - validation failed (?)");
        return false;
    }

    if (!occupant->SetHighlight(0x9, true)) {
        LOG_WARN("Failed to set prop highlight");
        return false;
    }

    occupant->AddRef();
    if (batchingPlacements_) {
        currentUndoGroup_.props.emplace_back(occupant);
    }
    else {
        UndoGroup group;
        group.props.emplace_back(occupant);
        undoStack_.push_back(std::move(group));
    }

    LOG_INFO("Placed prop 0x{:08X} at ({:.2f}, {:.2f}, {:.2f}), rotation: {}",
             propToCreate, placePos.fX, placePos.fY, placePos.fZ, rotation & 3);
    return true;
}

size_t PropPainterInputControl::PendingPlacementCount_() const {
    size_t count = 0;
    for (const auto& group : undoStack_) {
        count += group.props.size();
    }
    return count;
}

cISTETerrain* PropPainterInputControl::GetTerrain_() const {
    if (!city_) {
        return nullptr;
    }
    return city_->GetTerrain();
}

bool PropPainterInputControl::ShouldShowOutlinePreview_() const {
    if (settings_.previewMode == PropPreviewMode::Hidden) {
        return false;
    }

    if (state_ == ControlState::ActiveDirect) {
        return settings_.showGrid || settings_.previewMode != PropPreviewMode::FullModel;
    }

    return state_ == ControlState::ActiveLine || state_ == ControlState::ActivePolygon;
}

bool PropPainterInputControl::ShouldShowModelPreview_() const {
    return settings_.previewMode != PropPreviewMode::Hidden &&
        state_ == ControlState::ActiveDirect &&
        (settings_.previewMode == PropPreviewMode::FullModel || settings_.previewMode == PropPreviewMode::Combined);
}

void PropPainterInputControl::CreatePreviewProp_() {
    if (!propManager_) {
        LOG_WARN("Cannot create preview prop: prop manager not available");
        return;
    }

    const uint32_t previewPropID = CurrentDirectPropID_();
    if (previewPropID == 0) {
        LOG_WARN("Cannot create preview prop: no target prop selected");
        return;
    }

    if (previewProp_) {
        return;
    }

    cISC4PropOccupant* prop = nullptr;
    if (!propManager_->CreateProp(previewPropID, prop)) {
        LOG_WARN("Failed to create prop for preview");
        return;
    }

    cISC4Occupant* occupant = prop->AsOccupant();
    if (!occupant) {
        LOG_WARN("Failed to get occupant interface for preview prop");
        prop->Release();
        return;
    }

    cRZAutoRefCount<cISC4PropOccupant> previewProp(prop);
    cRZAutoRefCount<cISC4Occupant> previewOccupant(occupant, cRZAutoRefCount<cISC4Occupant>::kAddRef);

    cS3DVector3 initialPos(0.0f, 1000.0f, 0.0f);
    if (!previewOccupant->SetPosition(&initialPos)) {
        LOG_WARN("Failed to initialize preview prop position");
        return;
    }

    const int32_t normalizedRotation = settings_.rotation & 3;
    if (!previewProp->SetOrientation(normalizedRotation)) {
        LOG_WARN("Failed to initialize preview prop orientation");
        return;
    }

    if (!propManager_->AddCityProp(previewOccupant)) {
        LOG_WARN("Failed to add preview prop to city");
        return;
    }

    lastPreviewPosition_ = initialPos;
    lastPreviewRotation_ = normalizedRotation;
    previewPositionValid_ = false;
    previewOccupant->SetHighlight(0x3, true);
    previewOccupant->SetVisibility(false, true);

    previewProp_ = std::move(previewProp);
    previewOccupant_ = std::move(previewOccupant);
    previewPropID_ = previewPropID;
    previewActive_ = true;
    LOG_INFO("Created preview prop");
}

void PropPainterInputControl::DestroyPreviewProp_() {
    if (!previewOccupant_) {
        previewProp_.Reset();
        previewActive_ = false;
        return;
    }

    previewOccupant_->SetVisibility(false, true);
    if (propManager_) {
        propManager_->RemovePropA(previewOccupant_);
    }

    previewOccupant_.Reset();
    previewProp_.Reset();
    previewPropID_ = 0;
    previewActive_ = false;
    previewPositionValid_ = false;
    LOG_INFO("Destroyed preview prop");
}

void PropPainterInputControl::UpdatePreviewPropRotation_() {
    if (!previewActive_ || !previewProp_ || !previewOccupant_) {
        return;
    }

    const int32_t normalizedRotation = settings_.rotation & 3;
    if (normalizedRotation != lastPreviewRotation_) {
        if (previewProp_->SetOrientation(normalizedRotation)) {
            lastPreviewRotation_ = normalizedRotation;
        }
        else {
            LOG_WARN("Failed to update preview prop orientation");
        }
    }

    previewOccupant_->SetHighlight(0x2, false);
    previewOccupant_->SetHighlight(0x3, true);
}

void PropPainterInputControl::UpdatePreviewProp_() {
    if (!previewActive_ || !previewOccupant_) {
        return;
    }

    if (!cursorValid_) {
        previewOccupant_->SetVisibility(false, true);
        return;
    }

    cS3DVector3 worldPos = currentCursorWorld_;
    worldPos.fY += settings_.deltaYMeters;

    const bool posChanged =
        std::abs(worldPos.fX - lastPreviewPosition_.fX) > 0.05f ||
        std::abs(worldPos.fY - lastPreviewPosition_.fY) > 0.05f ||
        std::abs(worldPos.fZ - lastPreviewPosition_.fZ) > 0.05f;

    if (posChanged) {
        if (previewOccupant_->SetPosition(&worldPos)) {
            lastPreviewPosition_ = worldPos;
            previewPositionValid_ = true;
        }
        else {
            LOG_WARN("Failed to update preview prop position");
        }
    }
    else {
        previewPositionValid_ = true;
    }

    previewOccupant_->SetVisibility(true, true);
}
