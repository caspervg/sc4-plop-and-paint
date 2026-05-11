#include "DecalPickerInputControl.hpp"

#include <vector>
#include <windows.h>

#include "cISTETerrain.h"
#include "../utils/Logger.h"

namespace {
    constexpr auto  kDecalPickerControlID = 0x7F6E5D4Cu;
    constexpr float kPickRadiusMeters     = 8.0f;
}

DecalPickerInputControl::DecalPickerInputControl()
    : cSC4BaseViewInputControl(kDecalPickerControlID) {}

bool DecalPickerInputControl::Init() {
    if (initialized) {
        return true;
    }
    if (!cSC4BaseViewInputControl::Init()) {
        return false;
    }
    LOG_INFO("DecalPickerInputControl initialized");
    return true;
}

bool DecalPickerInputControl::Shutdown() {
    if (!initialized) {
        return true;
    }
    ClearHoveredDecal_();
    cSC4BaseViewInputControl::Shutdown();
    LOG_INFO("DecalPickerInputControl shut down");
    return true;
}

bool DecalPickerInputControl::OnMouseDownL(const int32_t /*x*/, const int32_t /*z*/, const uint32_t /*modifiers*/) {
    if (!active_ || !IsOnTop()) {
        return false;
    }
    if (!cursorValid_) {
        return false;
    }

    const TerrainDecalId id = PickNearestDecal_();
    if (id.value == 0 || !decalService_) {
        return false;
    }

    TerrainDecalSnapshot snap{};
    if (!decalService_->GetDecal(id, &snap)) {
        LOG_WARN("DecalPickerInputControl: failed to read decal id={}", id.value);
        return false;
    }

    const uint32_t instanceId = snap.state.textureKey.instance;
    if (instanceId == 0) {
        return false;
    }

    ClearHoveredDecal_();
    pickedInstanceId_ = instanceId;
    pickPending_ = true;
    LOG_INFO("DecalPickerInputControl: picked decal 0x{:08X}", instanceId);
    return true;
}

bool DecalPickerInputControl::OnMouseDownR(const int32_t /*x*/, const int32_t /*z*/, const uint32_t /*modifiers*/) {
    if (!active_ || !IsOnTop()) {
        return false;
    }
    ClearHoveredDecal_();
    cancelPending_ = true;
    return true;
}

bool DecalPickerInputControl::OnMouseMove(const int32_t x, const int32_t z, const uint32_t /*modifiers*/) {
    if (!active_ || !IsOnTop()) {
        return false;
    }
    UpdateCursorWorldFromScreen_(x, z);
    SetHoveredDecal_(PickNearestDecal_());
    BuildOverlay_();
    return true;
}

bool DecalPickerInputControl::OnKeyDown(const int32_t vkCode, const uint32_t /*modifiers*/) {
    if (!active_ || !IsOnTop()) {
        return false;
    }
    if (vkCode == VK_ESCAPE) {
        ClearHoveredDecal_();
        cancelPending_ = true;
        return true;
    }
    return false;
}

void DecalPickerInputControl::Activate() {
    cSC4BaseViewInputControl::Activate();
    if (!Init()) {
        LOG_WARN("DecalPickerInputControl: Init failed during Activate");
        return;
    }
    active_ = true;
    LOG_INFO("DecalPickerInputControl activated");
}

void DecalPickerInputControl::Deactivate() {
    active_ = false;
    ClearHoveredDecal_();
    overlay_.Clear();
    cSC4BaseViewInputControl::Deactivate();
    LOG_INFO("DecalPickerInputControl deactivated");
}

void DecalPickerInputControl::SetDecalService(cIGZTerrainDecalService* service) {
    decalService_ = service;
    if (!service) {
        ClearHoveredDecal_();
    }
}

void DecalPickerInputControl::SetCity(cISC4City* pCity) {
    city_ = pCity;
}

void DecalPickerInputControl::SetOnPick(std::function<void(uint32_t)> onPick) {
    onPick_ = std::move(onPick);
}

void DecalPickerInputControl::SetOnCancel(std::function<void()> onCancel) {
    onCancel_ = std::move(onCancel);
}

void DecalPickerInputControl::ProcessPendingActions() {
    if (pickPending_) {
        pickPending_ = false;
        if (onPick_) {
            onPick_(pickedInstanceId_);
        }
        // Auto-stop after a successful pick
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

void DecalPickerInputControl::DrawOverlay(IDirect3DDevice7* device) {
    if (!device) {
        return;
    }
    overlay_.Draw(device, false);
}

bool DecalPickerInputControl::UpdateCursorWorldFromScreen_(const int32_t screenX, const int32_t screenZ) {
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

TerrainDecalId DecalPickerInputControl::PickNearestDecal_() const {
    if (!cursorValid_ || !decalService_) {
        return TerrainDecalId{};
    }

    const uint32_t count = decalService_->GetDecalCount();
    if (count == 0) {
        return TerrainDecalId{};
    }

    std::vector<TerrainDecalSnapshot> snapshots(count);
    const uint32_t copied = decalService_->CopyDecals(snapshots.data(), count);

    TerrainDecalId nearest{};
    float nearestDistSq = kPickRadiusMeters * kPickRadiusMeters;

    for (uint32_t i = 0; i < copied; ++i) {
        const cS3DVector2& center = snapshots[i].state.decalInfo.center;
        const float dx = center.fX - currentCursorWorld_.fX;
        const float dz = center.fY - currentCursorWorld_.fZ;
        const float distSq = dx * dx + dz * dz;
        if (distSq < nearestDistSq) {
            nearestDistSq = distSq;
            nearest = snapshots[i].id;
        }
    }

    return nearest;
}

void DecalPickerInputControl::SetHoveredDecal_(const TerrainDecalId id) {
    if (hasHoveredDecal_ && hoveredDecalId_.value == id.value) {
        return;
    }

    if (hasHoveredDecal_ && decalService_) {
        TerrainDecalSnapshot snap{};
        if (decalService_->GetDecal(hoveredDecalId_, &snap)) {
            snap.state.drawMode = hoveredOriginalDrawMode_;
            decalService_->ReplaceDecal(hoveredDecalId_, snap.state);
        }
    }

    hasHoveredDecal_ = (id.value != 0);
    hoveredDecalId_  = id;
    hoveredOriginalDrawMode_ = 0;

    if (hasHoveredDecal_ && decalService_) {
        TerrainDecalSnapshot snap{};
        if (decalService_->GetDecal(id, &snap)) {
            hoveredOriginalDrawMode_ = snap.state.drawMode;
            snap.state.drawMode = 1;
            decalService_->ReplaceDecal(id, snap.state);
        }
    }
}

void DecalPickerInputControl::ClearHoveredDecal_() {
    SetHoveredDecal_(TerrainDecalId{});
}

void DecalPickerInputControl::BuildOverlay_() {
    if (!cursorValid_) {
        overlay_.Clear();
        return;
    }
    overlay_.BuildStripperPreview(hasHoveredDecal_, currentCursorWorld_, kPickRadiusMeters, GetTerrain_());
}

cISTETerrain* DecalPickerInputControl::GetTerrain_() const {
    if (!city_) {
        return nullptr;
    }
    return city_->GetTerrain();
}
