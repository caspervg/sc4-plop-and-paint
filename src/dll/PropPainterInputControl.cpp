#include "PropPainterInputControl.hpp"

#include <windows.h>

#include "cISC4Occupant.h"
#include "spdlog/spdlog.h"

namespace {
    constexpr auto kPropPainterControlID = 0x8A3F9D2B;
}

PropPainterInputControl::PropPainterInputControl()
    : cSC4BaseViewInputControl(kPropPainterControlID)
      , propIDToPaint_(0)
      , settings_({})
      , isPainting_(false)
      , onCancel_() {}

PropPainterInputControl::~PropPainterInputControl() {
    spdlog::info("PropPainterInputControl destroyed");
};

bool PropPainterInputControl::Init() {
    if (!cSC4BaseViewInputControl::Init()) {
        return false;
    }

    spdlog::info("PropPainterInputControl initialized");
    return true;
}

bool PropPainterInputControl::Shutdown() {
    spdlog::info("PropPainterInputControl shutting down");
    DestroyPreviewProp_();
    CancelAllPlacements();
    return cSC4BaseViewInputControl::Shutdown();
}

bool PropPainterInputControl::OnMouseDownL(int32_t x, int32_t z, uint32_t modifiers) {
    if (!isPainting_ || propIDToPaint_ == 0) {
        return false;
    }

    switch (settings_.mode) {
    case PropPaintMode::Direct:
        return PlacePropAt_(x, z);
    case PropPaintMode::Line:
    case PropPaintMode::Polygon:
        return true;
    default:
        return false;
    }
}

bool PropPainterInputControl::OnMouseMove(const int32_t x, const int32_t z, uint32_t modifiers) {
    if (!isPainting_) {
        return false;
    }

    UpdatePreviewProp_(x, z);
    return true;
}

bool PropPainterInputControl::OnKeyDown(const int32_t vkCode, uint32_t modifiers) {
    if (vkCode == VK_ESCAPE) {
        CancelAllPlacements();
        spdlog::info("PropPainterInputControl: ESC pressed, stopping paint mode");
        isPainting_ = false;
        if (onCancel_) onCancel_();
        return true;
    }

    if (vkCode == 'R') {
        settings_.rotation = (settings_.rotation + 1) % 4;
        UpdatePreviewPropRotation_();
        return true;
    }

    if (vkCode == 'Z' && modifiers & MOD_CONTROL) {
        UndoLastPlacement();
        return true;
    }

    if (vkCode == VK_RETURN) {
        CommitPlacements();
        return true;
    }

    if (vkCode == 'H') {
        if (!placedProps_.empty()) {
            const auto& last = placedProps_.back();
            const uint32_t currentHighlight = last->GetHighlight();
            last->SetHighlight((currentHighlight + 1) % 10, true);
            spdlog::log(spdlog::level::info, "Cycled highlight to {}", (currentHighlight + 1) % 10);
        }

        return true;
    }

    if (vkCode == 'P') {
        previewSettings_.showPreview = !previewSettings_.showPreview;
        spdlog::info("Toggled preview visibility: {}", previewSettings_.showPreview);
        return true;
    }

    return false;
}

void PropPainterInputControl::Activate() {
    cSC4BaseViewInputControl::Activate();
    isPainting_ = true;
    spdlog::info("PropPainterInputControl activated");
}

void PropPainterInputControl::Deactivate() {
    isPainting_ = false;
    DestroyPreviewProp_();
    cSC4BaseViewInputControl::Deactivate();
    spdlog::info("PropPainterInputControl deactivated");
}

void PropPainterInputControl::SetPropToPaint(uint32_t propID, const PropPaintSettings& settings,
                                             const std::string& name) {
    propIDToPaint_ = propID;
    settings_ = settings;
    spdlog::info("Set prop to paint: {} (0x{:08X}), rotation: {}", name, propID, settings.rotation);
    DestroyPreviewProp_();
    CreatePreviewProp_();
    spdlog::info("Preview prop created");
}

void PropPainterInputControl::SetCity(cISC4City* pCity) {
    city_ = pCity;
    if (pCity) {
        propManager_ = pCity->GetPropManager();
    }
}

void PropPainterInputControl::SetCameraService(cIGZS3DCameraService* cameraService) {
    cameraService_ = cameraService;
}

void PropPainterInputControl::SetOnCancel(std::function<void()> onCancel) {
    onCancel_ = std::move(onCancel);
}

void PropPainterInputControl::UndoLastPlacement() {
    if (placedProps_.empty()) {
        spdlog::debug("No props to undo");
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
    if (placedProps_.empty()) {
        return;
    }

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

void PropPainterInputControl::CommitPlacements() {
    spdlog::info("Committing {} placed props", placedProps_.size());
    for (const auto& prop : placedProps_) {
        prop->SetHighlight(0x0, true);
    }
    placedProps_.clear();
}

bool PropPainterInputControl::PlacePropAt_(int32_t screenX, int32_t screenZ) {
    if (!propManager_ || !view3D) {
        spdlog::warn("PropPainterInputControl: PropManager or View3D not available");
        return false;
    }

    float worldCoords[3] = {0.0f, 0.0f, 0.0f};
    if (!view3D->PickTerrain(screenX, screenZ, worldCoords, false)) {
        spdlog::debug("Failed to pick terrain at screen ({}, {})", screenX, screenZ);
        return false;
    }

    cS3DVector3 position(worldCoords[0], worldCoords[1], worldCoords[2]);

    cISC4PropOccupant* prop = nullptr;
    if (!propManager_->CreateProp(propIDToPaint_, prop)) {
        spdlog::warn("Failed to create prop 0x{:08X}", propIDToPaint_);
        return false;
    }

    cRZAutoRefCount propRef(prop);
    cISC4Occupant* occupant = prop->AsOccupant();

    if (!occupant->SetPosition(&position)) {
        spdlog::warn("Failed to set prop position");
        return false;
    }

    if (!prop->SetOrientation(settings_.rotation)) {
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
                 propIDToPaint_, position.fX, position.fY, position.fZ, settings_.rotation);

    return true;
}

void PropPainterInputControl::CreatePreviewProp_() {
    if (!propManager_ || previewProp_) {
        spdlog::warn("Preview prop already created");
        return;
    }

    cISC4PropOccupant* prop = nullptr;
    if (!propManager_->CreateProp(propIDToPaint_, prop)) {
        spdlog::warn("Failed to create prop for preview");
        return;
    }

    previewProp_ = cRZAutoRefCount<cISC4PropOccupant>(prop);
    previewOccupant_ = cRZAutoRefCount<cISC4Occupant>(prop->AsOccupant());

    cS3DVector3 initialPos(0, 1000, 0); // Way above ground
    lastPreviewPosition_ = initialPos;
    previewOccupant_->SetPosition(&initialPos);
    previewProp_->SetOrientation(0);
    lastPreviewRotation_ = 0;

    if (!propManager_->AddCityProp(previewOccupant_)) {
        spdlog::warn("Failed to add prop to city - validation failed (?)");
        previewProp_.Reset();
        previewOccupant_.Reset();
        return;
    }

    previewOccupant_->SetVisibility(true, true);
    previewOccupant_->SetHighlight(0x3, true);
    previewActive_ = true;
    spdlog::info("Created preview prop in CreatePreviewProp");
}

void PropPainterInputControl::DestroyPreviewProp_() {
    return;

    if (!previewOccupant_) return;

    if (propManager_) {
        propManager_->RemovePropA(previewOccupant_);
    }

    previewProp_.Reset();
    previewOccupant_.Reset();
    previewActive_ = false;

    spdlog::info("Destroyed preview prop");
}

void PropPainterInputControl::UpdatePreviewPropRotation_() {
    if (!previewActive_ || !previewOccupant_ || !view3D) {
        spdlog::warn("Cannot update preview prop - not active or missing components: {} {} {}", previewActive_, (void*)previewOccupant_, (void*)view3D);
        return;
    }

    if (settings_.rotation != lastPreviewRotation_) {
        previewProp_->SetOrientation(settings_.rotation);
        lastPreviewRotation_ = settings_.rotation;
    }
    previewOccupant_->SetHighlight(0x2, false);
    previewOccupant_->SetHighlight(0x3, true);
}

void PropPainterInputControl::UpdatePreviewProp_(int32_t screenX, int32_t screenZ) {
    if (!previewActive_ || !previewOccupant_ || !view3D) {
        spdlog::warn("Cannot update preview prop - not active or missing components: {} {} {}", previewActive_, (void*)previewOccupant_, (void*)view3D);
        return;
    }

    float worldCoords[3] = {0.0f, 0.0f, 0.0f};
    if (!view3D->PickTerrain(screenX, screenZ, worldCoords, false)) {
        // Mouse not on valid terrain, hide!
        previewOccupant_->SetVisibility(false, true);
        return;
    }

    cS3DVector3 worldPos(worldCoords[0], worldCoords[1], worldCoords[2]);

    const auto posChanged =
        std::abs(worldPos.fX - lastPreviewPosition_.fX) > 0.05f ||
        std::abs(worldPos.fY - lastPreviewPosition_.fY) > 0.05f ||
        std::abs(worldPos.fZ - lastPreviewPosition_.fZ) > 0.05f;

    const auto rotChanged = settings_.rotation != lastPreviewRotation_;

    if (posChanged || rotChanged) {
        previewOccupant_->SetPosition(&worldPos);
        lastPreviewPosition_ = worldPos;

        if (rotChanged) {
            previewProp_->SetOrientation(settings_.rotation);
            lastPreviewRotation_ = settings_.rotation;
        }

        previewOccupant_->SetHighlight(0x2, false);
        previewOccupant_->SetHighlight(0x3, true);
    }
    previewOccupant_->SetVisibility(true, true);
}
