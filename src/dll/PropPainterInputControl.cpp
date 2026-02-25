#include "PropPainterInputControl.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <windows.h>

#include "PropLinePlacer.hpp"
#include "PropPolygonPlacer.hpp"
#include "WeightedPropPicker.hpp"
#include "cISC4Occupant.h"
#include "cISTETerrain.h"
#include "spdlog/spdlog.h"

namespace {
    constexpr auto kPropPainterControlID = 0x8A3F9D2B;
    constexpr size_t kMaxPreviewPlacements = 5000;
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
    spdlog::info("PropPainterInputControl initialized");
    return true;
}

bool PropPainterInputControl::Shutdown() {
    if (state_ == ControlState::Uninitialized) {
        return true;
    }

    spdlog::info("PropPainterInputControl shutting down");
    CancelAllPlacements();
    DestroyPreviewProp_();
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
        spdlog::warn("PropPainterInputControl: Init failed during Activate");
        return;
    }

    TransitionTo_(propIDToPaint_ != 0 ? ActiveStateForMode_(settings_.mode) : ControlState::ActiveNoTarget, "Activate");
    spdlog::info("PropPainterInputControl activated");
}

void PropPainterInputControl::Deactivate() {
    ClearCollectedPoints_();
    DestroyPreviewProp_();

    if (state_ != ControlState::Uninitialized) {
        TransitionTo_(propIDToPaint_ != 0 ? ControlState::ReadyWithTarget : ControlState::ReadyNoTarget,
                      "Deactivate");
    }

    cSC4BaseViewInputControl::Deactivate();
    spdlog::info("PropPainterInputControl deactivated");
}

void PropPainterInputControl::SetPropToPaint(const uint32_t propID, const PropPaintSettings& settings,
                                             const std::string& name) {
    const bool targetChanged = propIDToPaint_ != propID;

    propIDToPaint_ = propID;
    settings_ = settings;
    if (settings_.randomSeed == 0) {
        settings_.randomSeed = static_cast<uint32_t>(GetTickCount64() ^ static_cast<uint64_t>(propIDToPaint_));
    }
    spdlog::info("Setting prop to paint: {} (0x{:08X}), rotation: {}", name, propID, settings.rotation);

    if (targetChanged) {
        ClearCollectedPoints_();
        DestroyPreviewProp_();
    }

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

void PropPainterInputControl::SetOnCancel(std::function<void()> onCancel) {
    onCancel_ = std::move(onCancel);
}

void PropPainterInputControl::DrawOverlay(IDirect3DDevice7* device) {
    if (!device || !previewSettings_.showPreview) {
        return;
    }

    if (state_ == ControlState::ActiveLine || state_ == ControlState::ActivePolygon) {
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
    spdlog::debug("PropPainterInputControl state transition: {} -> {} ({})",
                  StateToString_(oldState), StateToString_(newState), reason);
    SyncPreviewForState_();
}

void PropPainterInputControl::SyncPreviewForState_() {
    const bool showDirectPreview = state_ == ControlState::ActiveDirect && previewSettings_.showPreview;
    if (!showDirectPreview) {
        if (previewOccupant_) {
            previewOccupant_->SetVisibility(false, true);
        }
        if (state_ != ControlState::ActiveDirect) {
            DestroyPreviewProp_();
        }
    }
    else {
        if (!previewProp_) {
            CreatePreviewProp_();
        }
        else if (previewOccupant_) {
            previewOccupant_->SetVisibility(true, true);
            UpdatePreviewPropRotation_();
        }
    }

    if (!previewSettings_.showPreview || (state_ != ControlState::ActiveLine && state_ != ControlState::ActivePolygon)) {
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

    if (state_ == ControlState::ActiveDirect) {
        UpdatePreviewProp_(x, z);
    }
    else if (previewSettings_.showPreview) {
        RebuildPreviewOverlay_();
    }

    return true;
}

bool PropPainterInputControl::HandleActiveKeyDown_(const int32_t vkCode, const uint32_t modifiers) {
    if (vkCode == VK_ESCAPE) {
        ClearCollectedPoints_();
        CancelAllPlacements();
        spdlog::info("PropPainterInputControl: ESC pressed, stopping paint mode");
        TransitionTo_(propIDToPaint_ != 0 ? ControlState::ReadyWithTarget : ControlState::ReadyNoTarget,
                      "ESC cancel");
        if (onCancel_) {
            onCancel_();
        }
        return true;
    }

    if (!IsTargetActiveState_(state_)) {
        return false;
    }

    if (vkCode == 'R') {
        settings_.rotation = (settings_.rotation + 1) & 3;
        if (state_ == ControlState::ActiveDirect) {
            UpdatePreviewPropRotation_();
        }
        else {
            RebuildPreviewOverlay_();
        }
        return true;
    }

    if (vkCode == 'Z' && (modifiers & MOD_CONTROL)) {
        UndoLastPlacement();
        return true;
    }

    if (vkCode == VK_BACK && (state_ == ControlState::ActiveLine || state_ == ControlState::ActivePolygon)) {
        if (!collectedPoints_.empty()) {
            collectedPoints_.pop_back();
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
        previewSettings_.showPreview = !previewSettings_.showPreview;
        spdlog::info("Toggled preview visibility: {}", previewSettings_.showPreview);
        SyncPreviewForState_();
        return true;
    }

    return false;
}

bool PropPainterInputControl::UpdateCursorWorldFromScreen_(const int32_t screenX, const int32_t screenZ) {
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

void PropPainterInputControl::ClearCollectedPoints_() {
    collectedPoints_.clear();
    cursorValid_ = false;
    overlay_.Clear();
}

void PropPainterInputControl::RebuildPreviewOverlay_() {
    if (!previewSettings_.showPreview || (state_ != ControlState::ActiveLine && state_ != ControlState::ActivePolygon)) {
        overlay_.Clear();
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

    std::vector<cS3DVector3> plannedPositions;
    plannedPositions.reserve(kMaxPreviewPlacements);

    std::unique_ptr<WeightedPropPicker> picker;
    uint32_t singlePropID = propIDToPaint_;
    if (!settings_.activePalette.empty()) {
        picker = std::make_unique<WeightedPropPicker>(settings_.activePalette, settings_.randomSeed);
        singlePropID = 0;
    }

    if (state_ == ControlState::ActiveLine) {
        if (previewPoints.size() >= 2) {
            const auto placements = PropLinePlacer::ComputePlacements(
                previewPoints,
                std::max(settings_.spacingMeters, 0.25f),
                settings_.rotation,
                settings_.alignToPath,
                settings_.randomOffset,
                GetTerrain_(),
                settings_.randomSeed,
                picker.get(),
                singlePropID,
                kMaxPreviewPlacements);

            for (const auto& placement : placements) {
                plannedPositions.push_back(placement.position);
            }
        }

        overlay_.BuildLinePreview(points, currentCursorWorld_, cursorValid_, plannedPositions);
        return;
    }

    if (previewPoints.size() >= 3) {
        const auto placements = PropPolygonPlacer::ComputePlacements(
            previewPoints,
            std::max(settings_.densityPer100Sqm, 0.1f),
            settings_.rotation,
            settings_.randomRotation,
            GetTerrain_(),
            settings_.randomSeed,
            picker.get(),
            singlePropID,
            kMaxPreviewPlacements);

        for (const auto& placement : placements) {
            plannedPositions.push_back(placement.position);
        }
    }

    overlay_.BuildPolygonPreview(points, currentCursorWorld_, cursorValid_, plannedPositions);
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
        settings_.randomOffset,
        GetTerrain_(),
        settings_.randomSeed,
        picker.get(),
        singlePropID);

    size_t placedCount = 0;
    for (const auto& placement : placements) {
        if (PlacePropAtWorld_(placement.position, placement.rotation, placement.propID)) {
            ++placedCount;
        }
    }

    spdlog::info("Line paint executed: placed {} / {} props", placedCount, placements.size());
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
        settings_.rotation,
        settings_.randomRotation,
        GetTerrain_(),
        settings_.randomSeed,
        picker.get(),
        singlePropID);

    size_t placedCount = 0;
    for (const auto& placement : placements) {
        if (PlacePropAtWorld_(placement.position, placement.rotation, placement.propID)) {
            ++placedCount;
        }
    }

    spdlog::info("Polygon paint executed: placed {} / {} props", placedCount, placements.size());
    ClearCollectedPoints_();
}

void PropPainterInputControl::UndoLastPlacement() {
    if (placedProps_.empty()) {
        spdlog::debug("No props to undo");
        return;
    }

    if (!propManager_) {
        spdlog::warn("No prop manager available during undo; clearing local placed prop history");
        placedProps_.clear();
        return;
    }

    const auto& lastProp = placedProps_.back();

    if (propManager_->RemovePropA(lastProp)) {
        spdlog::info("Removed last placed prop ({} remaining)", placedProps_.size() - 1);
    }
    else {
        spdlog::warn("Failed to remove last placed prop");
    }

    placedProps_.pop_back();
}

void PropPainterInputControl::CancelAllPlacements() {
    if (!placedProps_.empty()) {
        if (!propManager_) {
            spdlog::warn("No prop manager available during cancel; clearing local placed prop history");
            placedProps_.clear();
        }
        else {
            spdlog::info("Canceling {} placed props", placedProps_.size());

            for (auto& prop : placedProps_) {
                if (propManager_->RemovePropA(prop)) {
                    spdlog::debug("Removed placed prop");
                }
                else {
                    spdlog::warn("Failed to remove placed prop");
                }
            }

            placedProps_.clear();
        }
    }

    ClearCollectedPoints_();
}

void PropPainterInputControl::CommitPlacements() {
    spdlog::info("Committing {} placed props", placedProps_.size());
    for (const auto& prop : placedProps_) {
        prop->SetHighlight(0x0, true);
    }
    placedProps_.clear();
}

bool PropPainterInputControl::PlacePropAt_(const int32_t screenX, const int32_t screenZ) {
    if (!view3D) {
        spdlog::warn("PropPainterInputControl: View3D not available");
        return false;
    }

    float worldCoords[3] = {0.0f, 0.0f, 0.0f};
    if (!view3D->PickTerrain(screenX, screenZ, worldCoords, false)) {
        spdlog::debug("Failed to pick terrain at screen ({}, {})", screenX, screenZ);
        return false;
    }

    return PlacePropAtWorld_(cS3DVector3(worldCoords[0], worldCoords[1], worldCoords[2]), settings_.rotation,
                             propIDToPaint_);
}

bool PropPainterInputControl::PlacePropAtWorld_(const cS3DVector3& position, const int32_t rotation,
                                                const uint32_t propID) {
    if (!propManager_) {
        spdlog::warn("PropPainterInputControl: PropManager not available");
        return false;
    }

    const uint32_t propToCreate = propID != 0 ? propID : propIDToPaint_;
    if (propToCreate == 0) {
        spdlog::warn("PlacePropAtWorld_: no prop ID available");
        return false;
    }

    cISC4PropOccupant* prop = nullptr;
    if (!propManager_->CreateProp(propToCreate, prop)) {
        spdlog::warn("Failed to create prop 0x{:08X}", propToCreate);
        return false;
    }

    cRZAutoRefCount propRef(prop);
    cISC4Occupant* occupant = prop->AsOccupant();
    if (!occupant) {
        spdlog::warn("Failed to get occupant interface from created prop");
        return false;
    }

    cS3DVector3 placePos = position;
    if (!occupant->SetPosition(&placePos)) {
        spdlog::warn("Failed to set prop position");
        return false;
    }

    if (!prop->SetOrientation(rotation & 3)) {
        spdlog::warn("Failed to set prop orientation");
        return false;
    }

    if (!propManager_->AddCityProp(occupant)) {
        spdlog::warn("Failed to add prop to city - validation failed (?)");
        return false;
    }

    if (!occupant->SetHighlight(0x9, true)) {
        spdlog::warn("Failed to set prop highlight");
        return false;
    }

    occupant->AddRef();
    placedProps_.emplace_back(occupant);

    spdlog::info("Placed prop 0x{:08X} at ({:.2f}, {:.2f}, {:.2f}), rotation: {}",
                 propToCreate, placePos.fX, placePos.fY, placePos.fZ, rotation & 3);
    return true;
}

cISTETerrain* PropPainterInputControl::GetTerrain_() const {
    if (!city_) {
        return nullptr;
    }
    return city_->GetTerrain();
}

void PropPainterInputControl::CreatePreviewProp_() {
    if (!propManager_) {
        spdlog::warn("Cannot create preview prop: prop manager not available");
        return;
    }

    if (propIDToPaint_ == 0) {
        spdlog::warn("Cannot create preview prop: no target prop selected");
        return;
    }

    if (previewProp_) {
        spdlog::warn("Preview prop already created");
        return;
    }

    cISC4PropOccupant* prop = nullptr;
    if (!propManager_->CreateProp(propIDToPaint_, prop)) {
        spdlog::warn("Failed to create prop for preview");
        return;
    }

    cISC4Occupant* previewOccupant = prop->AsOccupant();
    if (!previewOccupant) {
        spdlog::warn("Failed to get occupant interface for preview prop");
        return;
    }

    previewProp_ = cRZAutoRefCount<cISC4PropOccupant>(prop);
    previewOccupant_ = cRZAutoRefCount<cISC4Occupant>(previewOccupant);

    cS3DVector3 initialPos(0, 1000, 0);
    lastPreviewPosition_ = initialPos;
    previewOccupant_->SetPosition(&initialPos);
    previewProp_->SetOrientation(settings_.rotation & 3);
    lastPreviewRotation_ = settings_.rotation & 3;

    if (!propManager_->AddCityProp(previewOccupant_)) {
        spdlog::warn("Failed to add preview prop to city");
        previewProp_.Reset();
        previewOccupant_.Reset();
        return;
    }

    previewOccupant_->SetVisibility(true, true);
    previewOccupant_->SetHighlight(0x3, true);
    previewActive_ = true;
    spdlog::info("Created preview prop");
}

void PropPainterInputControl::DestroyPreviewProp_() {
    if (!previewOccupant_) {
        return;
    }

    if (propManager_) {
        propManager_->RemovePropA(previewOccupant_);
    }

    previewOccupant_.Reset();
    previewProp_.Reset();
    previewActive_ = false;
    spdlog::info("Destroyed preview prop");
}

void PropPainterInputControl::UpdatePreviewPropRotation_() {
    if (!previewSettings_.showPreview || !previewActive_ || !previewOccupant_ || !previewProp_) {
        return;
    }

    const int32_t normalizedRotation = settings_.rotation & 3;
    if (normalizedRotation != lastPreviewRotation_) {
        previewProp_->SetOrientation(normalizedRotation);
        lastPreviewRotation_ = normalizedRotation;
    }
    previewOccupant_->SetHighlight(0x2, false);
    previewOccupant_->SetHighlight(0x3, true);
}

void PropPainterInputControl::UpdatePreviewProp_(const int32_t screenX, const int32_t screenZ) {
    if (!previewSettings_.showPreview || !previewActive_ || !previewOccupant_ || !view3D) {
        return;
    }

    float worldCoords[3] = {0.0f, 0.0f, 0.0f};
    if (!view3D->PickTerrain(screenX, screenZ, worldCoords, false)) {
        previewOccupant_->SetVisibility(false, true);
        return;
    }

    const cS3DVector3 worldPos(worldCoords[0], worldCoords[1], worldCoords[2]);
    const bool posChanged =
        std::abs(worldPos.fX - lastPreviewPosition_.fX) > 0.05f ||
        std::abs(worldPos.fY - lastPreviewPosition_.fY) > 0.05f ||
        std::abs(worldPos.fZ - lastPreviewPosition_.fZ) > 0.05f;

    const int32_t normalizedRotation = settings_.rotation & 3;
    const bool rotChanged = normalizedRotation != lastPreviewRotation_;

    if (posChanged || rotChanged) {
        previewOccupant_->SetPosition(&worldPos);
        lastPreviewPosition_ = worldPos;

        if (rotChanged && previewProp_) {
            previewProp_->SetOrientation(normalizedRotation);
            lastPreviewRotation_ = normalizedRotation;
        }

        previewOccupant_->SetHighlight(0x2, false);
        previewOccupant_->SetHighlight(0x3, true);
    }
    previewOccupant_->SetVisibility(true, true);
}
