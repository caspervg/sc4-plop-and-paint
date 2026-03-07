#include "BasePainterInputControl.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <windows.h>

#include "Constants.hpp"
#include "PropLinePlacer.hpp"
#include "PropPolygonPlacer.hpp"
#include "PropPainterInputControl.hpp"  // for PropPaintMode/PropPreviewMode/PropPaintSettings
#include "WeightedPropPicker.hpp"
#include "cISTETerrain.h"
#include "utils/Logger.h"

namespace {
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

BasePainterInputControl::BasePainterInputControl(const uint32_t controlID)
    : cSC4BaseViewInputControl(controlID) {}

BasePainterInputControl::~BasePainterInputControl() = default;

bool BasePainterInputControl::Init() {
    if (state_ != ControlState::Uninitialized) {
        return true;
    }

    if (!cSC4BaseViewInputControl::Init()) {
        return false;
    }

    TransitionTo_(typeToPaint_ != 0 ? ControlState::ReadyWithTarget : ControlState::ReadyNoTarget, "Init");
    return true;
}

bool BasePainterInputControl::Shutdown() {
    if (state_ == ControlState::Uninitialized) {
        return true;
    }

    CancelAllPlacements();
    TransitionTo_(ControlState::Uninitialized, "Shutdown");
    cSC4BaseViewInputControl::Shutdown();
    return true;
}

bool BasePainterInputControl::OnMouseDownL(const int32_t x, const int32_t z, const uint32_t modifiers) {
    if (!IsActiveState_(state_) || !IsOnTop()) {
        return false;
    }
    return HandleActiveMouseDownL_(x, z, modifiers);
}

bool BasePainterInputControl::OnMouseDownR(const int32_t /*x*/, const int32_t /*z*/, const uint32_t /*modifiers*/) {
    if (!IsActiveState_(state_) || !IsOnTop()) {
        return false;
    }
    CancelAllPlacements();
    cancelPending_ = true;
    return true;
}

bool BasePainterInputControl::OnMouseMove(const int32_t x, const int32_t z, const uint32_t modifiers) {
    if (!IsActiveState_(state_) || !IsOnTop()) {
        return false;
    }
    return HandleActiveMouseMove_(x, z, modifiers);
}

bool BasePainterInputControl::OnKeyDown(const int32_t vkCode, const uint32_t modifiers) {
    if (!IsActiveState_(state_) || !IsOnTop()) {
        return false;
    }
    return HandleActiveKeyDown_(vkCode, modifiers);
}

void BasePainterInputControl::Activate() {
    cSC4BaseViewInputControl::Activate();
    if (!Init()) {
        return;
    }
    TransitionTo_(typeToPaint_ != 0 ? ActiveStateForMode_(settings_.mode) : ControlState::ActiveNoTarget, "Activate");
}

void BasePainterInputControl::Deactivate() {
    ClearCollectedPoints_();
    DestroyPreviewOccupant_();

    if (state_ != ControlState::Uninitialized) {
        TransitionTo_(typeToPaint_ != 0 ? ControlState::ReadyWithTarget : ControlState::ReadyNoTarget, "Deactivate");
    }

    cSC4BaseViewInputControl::Deactivate();
}

void BasePainterInputControl::SetTypeToPaint(const uint32_t typeID, const PropPaintSettings& settings,
                                              const std::string& name) {
    const bool targetChanged = typeToPaint_ != typeID;

    typeToPaint_ = typeID;
    settings_ = settings;
    settings_.deltaYMeters = ClampDeltaY(settings_.deltaYMeters);
    settings_.densityVariation = std::clamp(settings_.densityVariation, 0.0f, 1.0f);
    if (settings_.randomSeed == 0) {
        settings_.randomSeed = static_cast<uint32_t>(GetTickCount64() ^ static_cast<uint64_t>(typeToPaint_));
    }
    ResetDirectPaintPicker_();

    if (targetChanged) {
        ClearCollectedPoints_();
        DestroyPreviewOccupant_();
    }
    cachedPolygonPlacements_.clear();
    polygonPreviewDirty_ = true;

    if (state_ == ControlState::Uninitialized) {
        return;
    }

    if (typeToPaint_ == 0) {
        TransitionTo_(IsActiveState_(state_) ? ControlState::ActiveNoTarget : ControlState::ReadyNoTarget,
                      "SetTypeToPaint clear target");
        return;
    }

    TransitionTo_(IsActiveState_(state_) ? ActiveStateForMode_(settings_.mode) : ControlState::ReadyWithTarget,
                  "SetTypeToPaint");
}

void BasePainterInputControl::SetCity(cISC4City* pCity) {
    city_ = pCity;
    if (!pCity) {
        DestroyPreviewOccupant_();
        ClearCollectedPoints_();
    }
    OnCityChanged_(pCity);
}

void BasePainterInputControl::SetCameraService(cIGZS3DCameraService* cameraService) {
    cameraService_ = cameraService;
}

void BasePainterInputControl::SetOnCancel(std::function<void()> onCancel) {
    onCancel_ = std::move(onCancel);
}

void BasePainterInputControl::ProcessPendingActions() {
    if (cancelPending_) {
        cancelPending_ = false;
        if (onCancel_) {
            onCancel_();
        }
    }
}

void BasePainterInputControl::DrawOverlay(IDirect3DDevice7* device) {
    if (!device || settings_.previewMode == PropPreviewMode::Hidden) {
        return;
    }

    if (state_ == ControlState::ActiveDirect ||
        state_ == ControlState::ActiveLine ||
        state_ == ControlState::ActivePolygon) {
        overlay_.Draw(device);
    }
}

// ── Undo / commit ────────────────────────────────────────────────────────────

void BasePainterInputControl::UndoLastPlacement() {
    if (undoStack_.empty()) {
        LOG_DEBUG("No items to undo");
        return;
    }

    UndoGroup group = std::move(undoStack_.back());
    undoStack_.pop_back();

    size_t removedCount = 0;
    for (auto it = group.props.rbegin(); it != group.props.rend(); ++it) {
        RemoveOccupant_(*it);
        ++removedCount;
    }

    LOG_INFO("Undid placement group: removed {} item(s), {} remaining", removedCount, PendingPlacementCount_());
}

bool BasePainterInputControl::IsInDirectPaintState_() const {
    return state_ == ControlState::ActiveDirect;
}

void BasePainterInputControl::TrimUndoStack_() {
    while (undoStack_.size() > Undo::kMaxUndoGroups) {
        for (const auto& occupant : undoStack_.front().props) {
            occupant->SetHighlight(0x0, true);
        }
        undoStack_.erase(undoStack_.begin());
        LOG_DEBUG("Undo stack trimmed: {} groups remaining", undoStack_.size());
    }
}

void BasePainterInputControl::UndoLastPlacementInGroup_() {
    if (undoStack_.empty()) {
        return;
    }

    UndoGroup& group = undoStack_.back();
    if (group.props.empty()) {
        undoStack_.pop_back();
        return;
    }

    RemoveOccupant_(group.props.back());
    group.props.pop_back();
    if (group.props.empty()) {
        undoStack_.pop_back();
    }
}

void BasePainterInputControl::CancelAllPlacements() {
    const size_t pendingCount = PendingPlacementCount_();
    if (pendingCount > 0) {
        LOG_INFO("Canceling {} placed items", pendingCount);
        for (auto groupIt = undoStack_.rbegin(); groupIt != undoStack_.rend(); ++groupIt) {
            for (auto it = groupIt->props.rbegin(); it != groupIt->props.rend(); ++it) {
                RemoveOccupant_(*it);
            }
        }
        undoStack_.clear();
    }

    batchingPlacements_ = false;
    currentUndoGroup_.props.clear();
    ClearCollectedPoints_();
}

void BasePainterInputControl::CommitPlacements() {
    const size_t pendingCount = PendingPlacementCount_();
    LOG_INFO("Committing {} placed items", pendingCount);
    for (const auto& group : undoStack_) {
        for (const auto& occupant : group.props) {
            occupant->SetHighlight(0x0, true);
        }
    }
    undoStack_.clear();
    batchingPlacements_ = false;
    currentUndoGroup_.props.clear();
}

// ── State machine ────────────────────────────────────────────────────────────

bool BasePainterInputControl::IsActiveState_(const ControlState state) {
    return state == ControlState::ActiveNoTarget ||
        state == ControlState::ActiveDirect ||
        state == ControlState::ActiveLine ||
        state == ControlState::ActivePolygon;
}

bool BasePainterInputControl::IsTargetActiveState_(const ControlState state) {
    return state == ControlState::ActiveDirect ||
        state == ControlState::ActiveLine ||
        state == ControlState::ActivePolygon;
}

BasePainterInputControl::ControlState BasePainterInputControl::ActiveStateForMode_(const PropPaintMode mode) {
    switch (mode) {
    case PropPaintMode::Direct:  return ControlState::ActiveDirect;
    case PropPaintMode::Line:    return ControlState::ActiveLine;
    case PropPaintMode::Polygon: return ControlState::ActivePolygon;
    default:                     return ControlState::ActiveDirect;
    }
}

const char* BasePainterInputControl::StateToString_(const ControlState state) {
    switch (state) {
    case ControlState::Uninitialized:   return "Uninitialized";
    case ControlState::ReadyNoTarget:   return "ReadyNoTarget";
    case ControlState::ReadyWithTarget: return "ReadyWithTarget";
    case ControlState::ActiveNoTarget:  return "ActiveNoTarget";
    case ControlState::ActiveDirect:    return "ActiveDirect";
    case ControlState::ActiveLine:      return "ActiveLine";
    case ControlState::ActivePolygon:   return "ActivePolygon";
    default:                            return "Unknown";
    }
}

void BasePainterInputControl::TransitionTo_(const ControlState newState, const char* reason) {
    if (state_ == newState) {
        SyncPreviewForState_();
        return;
    }
    const auto oldState = state_;
    state_ = newState;
    LOG_INFO("PainterInputControl state: {} -> {} ({})", StateToString_(oldState), StateToString_(newState), reason);
    SyncPreviewForState_();
}

void BasePainterInputControl::SyncPreviewForState_() {
    if (ShouldShowModelPreview_()) {
        if (HasActivePreviewOccupant_() && previewOccupantTypeID_ != CurrentDirectTypeID_()) {
            DestroyPreviewOccupant_();
        }
        if (!HasActivePreviewOccupant_()) {
            CreatePreviewOccupant_();
        }
        UpdatePreviewOccupantRotation_();
        UpdatePreviewOccupant_();
    }
    else {
        DestroyPreviewOccupant_();
    }

    if (!ShouldShowOutlinePreview_()) {
        overlay_.Clear();
    }
    else {
        RebuildPreviewOverlay_();
    }
}

bool BasePainterInputControl::HandleActiveMouseDownL_(const int32_t x, const int32_t z, uint32_t /*modifiers*/) {
    switch (state_) {
    case ControlState::ActiveDirect:
        return PlaceTypeAt_(x, z);
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

bool BasePainterInputControl::HandleActiveMouseMove_(const int32_t x, const int32_t z, uint32_t /*modifiers*/) {
    if (!IsTargetActiveState_(state_)) {
        return false;
    }
    UpdateCursorWorldFromScreen_(x, z);
    SyncPreviewForState_();
    return true;
}

bool BasePainterInputControl::HandleActiveKeyDown_(const int32_t vkCode, const uint32_t modifiers) {
    if (vkCode == VK_ESCAPE) {
        CancelAllPlacements();
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
        UndoLastPlacementInGroup_();
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
        const int mode = (static_cast<int>(settings_.previewMode) + 1) % 4;
        settings_.previewMode = static_cast<PropPreviewMode>(mode);
        SyncPreviewForState_();
        return true;
    }

    if (vkCode == 'G') {
        settings_.showGrid = !settings_.showGrid;
        SyncPreviewForState_();
        return true;
    }

    if (vkCode == 'S') {
        settings_.snapPointsToGrid = !settings_.snapPointsToGrid;
        if (!settings_.snapPointsToGrid) {
            settings_.snapPlacementsToGrid = false;
        }
        SyncPreviewForState_();
        return true;
    }

    if (vkCode == VK_OEM_4 || vkCode == VK_OEM_6) {
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
        }
        else if (vkCode == VK_OEM_6 && currentIdx < kGridStepCount - 1) {
            ++currentIdx;
        }

        settings_.gridStepMeters = kGridSteps[currentIdx];
        SyncPreviewForState_();
        return true;
    }

    if (vkCode == VK_OEM_MINUS || vkCode == VK_OEM_PLUS) {
        const float lineDelta = (vkCode == VK_OEM_PLUS) ? 0.5f : -0.5f;
        const float densityDelta = (vkCode == VK_OEM_PLUS) ? 0.25f : -0.25f;
        const bool controlHeld = (modifiers & MOD_CONTROL) != 0;

        if (state_ == ControlState::ActiveLine) {
            settings_.spacingMeters = std::clamp(settings_.spacingMeters + lineDelta, 0.5f, 50.0f);
        }
        else if (state_ == ControlState::ActivePolygon) {
            if (controlHeld) {
                const float variationDelta = (vkCode == VK_OEM_PLUS) ? 0.05f : -0.05f;
                settings_.densityVariation = std::clamp(settings_.densityVariation + variationDelta, 0.0f, 1.0f);
            }
            else {
                settings_.densityPer100Sqm = std::clamp(settings_.densityPer100Sqm + densityDelta, 0.1f, 10.0f);
            }
            polygonPreviewDirty_ = true;
        }
        else {
            return false;
        }

        SyncPreviewForState_();
        return true;
    }

    return false;
}

// ── Cursor / world helpers ────────────────────────────────────────────────────

bool BasePainterInputControl::UpdateCursorWorldFromScreen_(const int32_t screenX, const int32_t screenZ) {
    const bool hadValidCursor = cursorValid_;
    const cS3DVector3 previousCursorWorld = currentCursorWorld_;
    cursorValid_ = false;
    if (!view3D) {
        return false;
    }

    // Ask subclass to hide its preview before doing a terrain pick so the
    // pick ray doesn't intersect the preview model.
    if (state_ == ControlState::ActiveDirect && HasActivePreviewOccupant_()) {
        HidePreviewForPick_();
    }

    float worldCoords[3] = {0.0f, 0.0f, 0.0f};
    if (!view3D->PickTerrain(screenX, screenZ, worldCoords, false)) {
        if (hadValidCursor) {
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

float BasePainterInputControl::GetGridStepMeters_() const {
    return std::max(settings_.gridStepMeters, 1.0f);
}

cS3DVector3 BasePainterInputControl::SnapWorldToGrid_(const cS3DVector3& position) const {
    const float gridStep = GetGridStepMeters_();
    return {SnapCoordinate(position.fX, gridStep), position.fY, SnapCoordinate(position.fZ, gridStep)};
}

void BasePainterInputControl::SnapPlacementToGrid_(PlannedProp& placement) const {
    if (!settings_.snapPlacementsToGrid) {
        return;
    }
    placement.position = SnapWorldToGrid_(placement.position);
}

void BasePainterInputControl::ClearCollectedPoints_() {
    collectedPoints_.clear();
    cursorValid_ = false;
    cachedPolygonPlacements_.clear();
    polygonPreviewDirty_ = true;
    overlay_.Clear();
}

cISTETerrain* BasePainterInputControl::GetTerrain_() const {
    if (!city_) {
        return nullptr;
    }
    return city_->GetTerrain();
}

// ── Picker helpers ────────────────────────────────────────────────────────────

void BasePainterInputControl::ResetDirectPaintPicker_() {
    directPaintPicker_.reset();
    directPaintTypeID_ = typeToPaint_;

    if (settings_.activePalette.empty()) {
        return;
    }

    directPaintPicker_ = std::make_unique<WeightedPropPicker>(settings_.activePalette, settings_.randomSeed);
    directPaintTypeID_ = directPaintPicker_->Pick();
    if (directPaintTypeID_ == 0) {
        directPaintTypeID_ = typeToPaint_;
    }
}

uint32_t BasePainterInputControl::CurrentDirectTypeID_() const {
    return directPaintTypeID_ != 0 ? directPaintTypeID_ : typeToPaint_;
}

void BasePainterInputControl::AdvanceDirectPaintType_() {
    if (!directPaintPicker_) {
        directPaintTypeID_ = typeToPaint_;
        return;
    }
    directPaintTypeID_ = directPaintPicker_->Pick();
    if (directPaintTypeID_ == 0) {
        directPaintTypeID_ = typeToPaint_;
    }
}

// ── Placement ────────────────────────────────────────────────────────────────

void BasePainterInputControl::AddOccupantToUndo_(cISC4Occupant* occupant) {
    occupant->AddRef();
    if (batchingPlacements_) {
        currentUndoGroup_.props.emplace_back(occupant);
    }
    else {
        UndoGroup group;
        group.props.emplace_back(occupant);
        undoStack_.push_back(std::move(group));
        TrimUndoStack_();
    }
}

bool BasePainterInputControl::PlaceTypeAt_(const int32_t screenX, const int32_t screenZ) {
    if (!view3D) {
        return false;
    }

    float worldCoords[3] = {0.0f, 0.0f, 0.0f};
    if (!view3D->PickTerrain(screenX, screenZ, worldCoords, false)) {
        return false;
    }

    cS3DVector3 targetPosition(worldCoords[0], worldCoords[1], worldCoords[2]);
    if (settings_.snapPointsToGrid) {
        targetPosition = SnapWorldToGrid_(targetPosition);
    }
    targetPosition.fY += settings_.deltaYMeters;

    const bool placed = PlaceAtWorld_(targetPosition, settings_.rotation, CurrentDirectTypeID_());
    if (placed) {
        AdvanceDirectPaintType_();
        SyncPreviewForState_();
    }
    return placed;
}

void BasePainterInputControl::ExecuteLinePlacement_() {
    if (collectedPoints_.size() < 2) {
        return;
    }

    std::vector<cS3DVector3> linePoints;
    linePoints.reserve(collectedPoints_.size());
    for (const auto& cp : collectedPoints_) {
        linePoints.push_back(cp.worldPos);
    }

    std::unique_ptr<WeightedPropPicker> picker;
    uint32_t singleTypeID = typeToPaint_;
    if (!settings_.activePalette.empty()) {
        picker = std::make_unique<WeightedPropPicker>(settings_.activePalette, settings_.randomSeed);
        singleTypeID = 0;
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
        singleTypeID);

    batchingPlacements_ = true;
    currentUndoGroup_.props.clear();
    size_t placedCount = 0;
    for (auto placement : placements) {
        placement.position.fY += settings_.deltaYMeters;
        SnapPlacementToGrid_(placement);
        if (PlaceAtWorld_(placement.position, placement.rotation, placement.propID)) {
            ++placedCount;
        }
    }
    batchingPlacements_ = false;
    if (!currentUndoGroup_.props.empty()) {
        undoStack_.push_back(std::move(currentUndoGroup_));
        TrimUndoStack_();
    }
    currentUndoGroup_.props.clear();

    LOG_INFO("Line paint executed: placed {} / {} items", placedCount, placements.size());
    ClearCollectedPoints_();
}

void BasePainterInputControl::ExecutePolygonPlacement_() {
    if (collectedPoints_.size() < 3) {
        return;
    }

    std::vector<cS3DVector3> polygonVertices;
    polygonVertices.reserve(collectedPoints_.size());
    for (const auto& cp : collectedPoints_) {
        polygonVertices.push_back(cp.worldPos);
    }

    std::unique_ptr<WeightedPropPicker> picker;
    uint32_t singleTypeID = typeToPaint_;
    if (!settings_.activePalette.empty()) {
        picker = std::make_unique<WeightedPropPicker>(settings_.activePalette, settings_.randomSeed);
        singleTypeID = 0;
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
        singleTypeID);

    batchingPlacements_ = true;
    currentUndoGroup_.props.clear();
    size_t placedCount = 0;
    for (auto placement : placements) {
        placement.position.fY += settings_.deltaYMeters;
        SnapPlacementToGrid_(placement);
        if (PlaceAtWorld_(placement.position, placement.rotation, placement.propID)) {
            ++placedCount;
        }
    }
    batchingPlacements_ = false;
    if (!currentUndoGroup_.props.empty()) {
        undoStack_.push_back(std::move(currentUndoGroup_));
        TrimUndoStack_();
    }
    currentUndoGroup_.props.clear();

    LOG_INFO("Polygon paint executed: placed {} / {} items", placedCount, placements.size());
    ClearCollectedPoints_();
}

// ── Overlay ───────────────────────────────────────────────────────────────────

bool BasePainterInputControl::ShouldShowOutlinePreview_() const {
    if (settings_.previewMode == PropPreviewMode::Hidden) {
        return false;
    }
    if (state_ == ControlState::ActiveDirect) {
        return settings_.showGrid || settings_.previewMode != PropPreviewMode::FullModel;
    }
    return state_ == ControlState::ActiveLine || state_ == ControlState::ActivePolygon;
}

void BasePainterInputControl::RebuildPreviewOverlay_() {
    if (!ShouldShowOutlinePreview_()) {
        overlay_.Clear();
        return;
    }

    constexpr auto kMaxPreviewPlacements = 5000uz;

    if (state_ == ControlState::ActiveDirect) {
        const bool usePreviewAnchor = ShouldShowModelPreview_() && previewPositionValid_;
        const bool hasOverlayCursor = cursorValid_ || usePreviewAnchor;
        cS3DVector3 overlayCursor = currentCursorWorld_;
        if (!cursorValid_ && usePreviewAnchor) {
            overlayCursor = lastPreviewPosition_;
        }

        PropPaintOverlay::PreviewPlacement previewPlacement;
        previewPlacement.placement.position = overlayCursor;
        previewPlacement.placement.position.fY += settings_.deltaYMeters;
        previewPlacement.placement.rotation = settings_.rotation;
        previewPlacement.placement.propID = CurrentDirectTypeID_();

        if (hasOverlayCursor) {
            PopulatePreviewBounds_(previewPlacement, previewPlacement.placement.propID);
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
    uint32_t singleTypeID = typeToPaint_;
    if (!settings_.activePalette.empty()) {
        picker = std::make_unique<WeightedPropPicker>(settings_.activePalette, settings_.randomSeed);
        singleTypeID = 0;
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
                singleTypeID,
                kMaxPreviewPlacements);

            for (const auto& placement : placements) {
                PropPaintOverlay::PreviewPlacement pp;
                pp.placement = placement;
                pp.placement.position.fY += settings_.deltaYMeters;
                SnapPlacementToGrid_(pp.placement);
                PopulatePreviewBounds_(pp, pp.placement.propID);
                plannedPlacements.push_back(pp);
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
            singleTypeID,
            kMaxPreviewPlacements);

        for (const auto& placement : placements) {
            PropPaintOverlay::PreviewPlacement pp;
            pp.placement = placement;
            pp.placement.position.fY += settings_.deltaYMeters;
            SnapPlacementToGrid_(pp.placement);
            PopulatePreviewBounds_(pp, pp.placement.propID);
            cachedPolygonPlacements_.push_back(pp);
        }

        polygonPreviewDirty_ = false;
    }

    overlay_.BuildPolygonPreview(points, currentCursorWorld_, cursorValid_, GetTerrain_(), settings_,
                                  cachedPolygonPlacements_);
}

size_t BasePainterInputControl::PendingPlacementCount_() const {
    size_t count = 0;
    for (const auto& group : undoStack_) {
        count += group.props.size();
    }
    return count;
}
