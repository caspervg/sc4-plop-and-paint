#include "PropPainterInputControl.hpp"

#include <windows.h>

#include "spdlog/spdlog.h"

namespace {
    constexpr auto kPropPainterControlID = 0x8A3F9D2B;
}

PropPainterInputControl::PropPainterInputControl()
    : cSC4BaseViewInputControl(kPropPainterControlID)
      , propIDToPaint_(0)
      , settings_({})
      , isPainting_(false) {}

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
    return cSC4BaseViewInputControl::Shutdown();
}

bool PropPainterInputControl::OnMouseDownL(int32_t x, int32_t z, uint32_t modifiers) {
    if (!isPainting_ || propIDToPaint_ == 0) {
        return false;
    }

    switch (settings_.mode) {
    case PropPaintMode::Direct:
        return PlacePropAt(x, z);
    case PropPaintMode::Line:
    case PropPaintMode::Polygon:
        UpdatePreviewState(x, z);
        return true;
    default:
        return false;
    }
}

bool PropPainterInputControl::OnMouseMove(int32_t x, int32_t z, uint32_t modifiers) {
    if (!isPainting_) {
        return false;
    }

    UpdatePreviewState(x, z);
    return true;
}

bool PropPainterInputControl::OnKeyDown(int32_t vkCode, uint32_t modifiers) {
    if (vkCode == VK_ESCAPE) {
        spdlog::info("PropPainterInputControl: ESC pressed, stopping paint mode");
        isPainting_ = false;
        if (onCancel_) {
            onCancel_();
        }
        else if (view3D) {
            view3D->RemoveCurrentViewInputControl(false);
        }
        return true;
    }

    if (vkCode == 'R') {
        settings_.rotation = (settings_.rotation + 1) % 4;
        previewState_.rotation = settings_.rotation;
        spdlog::info("Rotated to: {}", settings_.rotation);
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
    cSC4BaseViewInputControl::Deactivate();
    spdlog::info("PropPainterInputControl deactivated");
}

void PropPainterInputControl::SetPropToPaint(uint32_t propID, const PropPaintSettings& settings,
                                             const std::string& name) {
    propIDToPaint_ = propID;
    settings_ = settings;
    previewState_.propID = propID;
    previewState_.rotation = settings.rotation;
    previewState_.propName = name;
    previewState_.paintMode = static_cast<int32_t>(settings.mode);
    spdlog::info("Set prop to paint: {} (0x{:08X}), rotation: {}", name, propID, settings.rotation);
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

bool PropPainterInputControl::PlacePropAt(int32_t screenX, int32_t screenZ) {
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

    spdlog::info("Placing prop 0x{:08X} at ({:.2f}, {:.2f}, {:.2f}), rotation: {}",
                 propIDToPaint_, position.fX, position.fY, position.fZ, settings_.rotation * 90.0f);

    const bool success = propManager_->AddCityProp(propIDToPaint_, position, settings_.rotation);
    if (!success) {
        spdlog::warn("Failed to place prop");
    }

    return success;
}

void PropPainterInputControl::UpdatePreviewState(int32_t screenX, int32_t screenZ) {
    if (!view3D) {
        previewState_.cursorValid = false;
        return;
    }

    float worldCoords[3] = {0.0f, 0.0f, 0.0f};
    previewState_.cursorValid = view3D->PickTerrain(screenX, screenZ, worldCoords, false);

    if (previewState_.cursorValid) {
        previewState_.cursorWorldPos.fX = worldCoords[0];
        previewState_.cursorWorldPos.fY = worldCoords[1];
        previewState_.cursorWorldPos.fZ = worldCoords[2];
        previewState_.rotation = settings_.rotation;
    }
}
