#include "DecalStripperInputControl.hpp"

#include <windows.h>

#include "cISTETerrain.h"
#include "../utils/Logger.h"

namespace {
    constexpr auto  kDecalStripperControlID = 0x6E5D4C3Bu;
    constexpr float kPickRadiusMeters       = 8.0f;
    constexpr float kBrushRadiusMeters      = 16.0f;
    constexpr DWORD kRectHighlightColor     = 0xF0FFD700;
}

DecalStripperInputControl::DecalStripperInputControl()
    : cSC4BaseViewInputControl(kDecalStripperControlID) {}

bool DecalStripperInputControl::Init() {
    if (initialized) {
        return true;
    }
    if (!cSC4BaseViewInputControl::Init()) {
        return false;
    }
    LOG_INFO("DecalStripperInputControl initialized");
    return true;
}

bool DecalStripperInputControl::Shutdown() {
    if (!initialized) {
        return true;
    }
    ClearHover_();
    undoStack_.clear();
    cSC4BaseViewInputControl::Shutdown();
    LOG_INFO("DecalStripperInputControl shut down");
    return true;
}

bool DecalStripperInputControl::OnMouseDownL(const int32_t /*x*/, const int32_t /*z*/, const uint32_t /*modifiers*/) {
    if (!active_ || !IsOnTop()) {
        return false;
    }
    if (stripMode_ == StripMode::Brush) {
        leftMouseDown_ = true;
        if (!cursorValid_) {
            return false;
        }
        DeleteDecalsInBrush_();
        BuildOverlay_();
        return true;
    }
    if (!cursorValid_) {
        return false;
    }
    RefreshHover_();
    if (DeleteHovered_()) {
        ClearHover_();
        BuildOverlay_();
        return true;
    }
    return false;
}

bool DecalStripperInputControl::OnMouseUpL(const int32_t /*x*/, const int32_t /*z*/, const uint32_t /*modifiers*/) {
    leftMouseDown_ = false;
    return active_ && IsOnTop();
}

bool DecalStripperInputControl::OnMouseDownR(const int32_t /*x*/, const int32_t /*z*/, const uint32_t /*modifiers*/) {
    if (!active_ || !IsOnTop()) {
        return false;
    }
    leftMouseDown_ = false;
    ClearHover_();
    LOG_INFO("DecalStripperInputControl: RMB pressed, exiting strip mode");
    cancelPending_ = true;
    return true;
}

bool DecalStripperInputControl::OnMouseMove(const int32_t x, const int32_t z, const uint32_t /*modifiers*/) {
    if (!active_ || !IsOnTop()) {
        return false;
    }
    UpdateCursorWorldFromScreen_(x, z);
    if (stripMode_ == StripMode::Brush) {
        ClearHover_();
        if (leftMouseDown_) {
            DeleteDecalsInBrush_();
        }
    }
    else {
        RefreshHover_();
    }
    BuildOverlay_();
    return true;
}

bool DecalStripperInputControl::OnMouseWheel(const int32_t x, const int32_t z, const uint32_t modifiers,
                                             const int32_t wheelDelta) {
    // Alt+scroll cycles the candidate stack under the cursor (stacked decals /
    // base+overlay textures on the same cell); plain scroll stays camera zoom.
    const bool altHeld = (GetKeyState(VK_MENU) & 0x8000) != 0;
    if (altHeld && active_ && IsOnTop() && stripMode_ == StripMode::Single &&
        pickStrategy_ && wheelDelta != 0 && pickStrategy_->CandidateCount() > 1) {
        pickStrategy_->CycleCandidates(wheelDelta > 0 ? 1 : -1);
        UpdateCursorWorldFromScreen_(x, z);
        RefreshHover_();
        BuildOverlay_();
        return true;
    }
    return cSC4BaseViewInputControl::OnMouseWheel(x, z, modifiers, wheelDelta);
}

bool DecalStripperInputControl::OnKeyDown(const int32_t vkCode, const uint32_t modifiers) {
    if (!active_ || !IsOnTop()) {
        return false;
    }

    if (vkCode == VK_ESCAPE) {
        leftMouseDown_ = false;
        ClearHover_();
        LOG_INFO("DecalStripperInputControl: ESC pressed, exiting strip mode");
        cancelPending_ = true;
        return true;
    }

    if (vkCode == 'Z' && (modifiers & MOD_CONTROL)) {
        UndoLastDeletion();
        return true;
    }

    if (vkCode == 'B') {
        stripMode_ = (stripMode_ == StripMode::Single) ? StripMode::Brush : StripMode::Single;
        leftMouseDown_ = false;
        ClearHover_();
        if (stripMode_ == StripMode::Brush) {
            LOG_INFO("DecalStripperInputControl: switched to brush mode");
        }
        else {
            RefreshHover_();
            LOG_INFO("DecalStripperInputControl: switched to single mode");
        }
        BuildOverlay_();
        return true;
    }

    return false;
}

void DecalStripperInputControl::Activate() {
    cSC4BaseViewInputControl::Activate();
    if (!Init()) {
        LOG_WARN("DecalStripperInputControl: Init failed during Activate");
        return;
    }
    active_ = true;
    LOG_INFO("DecalStripperInputControl activated");
}

void DecalStripperInputControl::Deactivate() {
    active_ = false;
    leftMouseDown_ = false;
    ProcessPendingActions();
    ClearHover_();
    overlay_.Clear();
    cSC4BaseViewInputControl::Deactivate();
    LOG_INFO("DecalStripperInputControl deactivated");
}

void DecalStripperInputControl::SetDecalService(cIGZTerrainDecalService* service) {
    decalService_ = service;
    if (!service) {
        ClearHover_();
        undoStack_.clear();
    }
}

void DecalStripperInputControl::SetCity(cISC4City* pCity) {
    city_ = pCity;
}

void DecalStripperInputControl::SetPickStrategy(std::unique_ptr<ScenePickStrategy> strategy) {
    ClearHover_();
    pickStrategy_ = std::move(strategy);
}

void DecalStripperInputControl::SetOnCancel(std::function<void()> onCancel) {
    onCancel_ = std::move(onCancel);
}

void DecalStripperInputControl::SetStripPersistence(
    std::function<void(const lottex::StripRecord&)> onAdded,
    std::function<void(const lottex::StripRecord&)> onRemoved) {
    onStripAdded_ = std::move(onAdded);
    onStripRemoved_ = std::move(onRemoved);
}

void DecalStripperInputControl::UndoLastDeletion() {
    if (undoStack_.empty()) {
        LOG_DEBUG("DecalStripperInputControl: nothing to undo");
        return;
    }

    UndoEntry entry = std::move(undoStack_.back());
    undoStack_.pop_back();

    if (auto* decalInfo = std::get_if<DeletedDecalInfo>(&entry)) {
        if (!decalService_) {
            LOG_WARN("DecalStripperInputControl: no service for decal undo");
            return;
        }
        TerrainDecalId newId{};
        if (!decalService_->CreateDecal(&decalInfo->state,
                                        static_cast<uint32_t>(sizeof(decalInfo->state)), &newId)) {
            LOG_WARN("DecalStripperInputControl: failed to recreate decal during undo");
            return;
        }
        LOG_INFO("DecalStripperInputControl: restored decal id={}, {} undo(s) remaining",
                 newId.value, undoStack_.size());
    }
    else if (auto* lotTex = std::get_if<lottex::RemovedLotTexture>(&entry)) {
        if (!lottex::RestoreLotTexture(city_, *lotTex)) {
            LOG_WARN("DecalStripperInputControl: failed to restore lot texture during undo");
            return;
        }
        if (onStripRemoved_) {
            onStripRemoved_(lotTex->record);
        }
        LOG_INFO("DecalStripperInputControl: restored lot texture, {} undo(s) remaining",
                 undoStack_.size());
    }
}

void DecalStripperInputControl::ProcessPendingActions() {
    if (cancelPending_) {
        cancelPending_ = false;
        if (onCancel_) {
            onCancel_();
        }
    }
}

bool DecalStripperInputControl::UpdateCursorWorldFromScreen_(const int32_t screenX, const int32_t screenZ) {
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

void DecalStripperInputControl::RefreshHover_() {
    if (!pickStrategy_ || !cursorValid_) {
        ClearHover_();
        return;
    }
    ScenePickContext context;
    context.city = city_;
    context.cursorWorld = currentCursorWorld_;
    hoveredResult_ = pickStrategy_->Pick(context);
    pickStrategy_->SetHover(hoveredResult_);
}

void DecalStripperInputControl::ClearHover_() {
    if (pickStrategy_) {
        pickStrategy_->ClearHover();
    }
    hoveredResult_.reset();
}

bool DecalStripperInputControl::DeleteHovered_() {
    if (!hoveredResult_) {
        return false;
    }
    const auto* decal = std::get_if<PickedDecal>(&*hoveredResult_);
    if (!decal) {
        return false;
    }

    if (decal->source == PickedDecalSource::Decal) {
        if (!decalService_) {
            return false;
        }
        const TerrainDecalId id{decal->decalId};
        TerrainDecalSnapshot snap{};
        if (!decalService_->GetDecal(id, &snap, static_cast<uint32_t>(sizeof(snap)))) {
            LOG_WARN("DecalStripperInputControl: failed to get decal before removal");
            return false;
        }
        if (!decalService_->RemoveDecal(id)) {
            LOG_WARN("DecalStripperInputControl: failed to remove decal id={}", id.value);
            return false;
        }
        undoStack_.push_back(DeletedDecalInfo{snap.state});
        LOG_INFO("DecalStripperInputControl: removed decal id={}, {} undo(s) available",
                 id.value, undoStack_.size());
        return true;
    }

    // Lot base/overlay texture: edit the per-lot occupant spec vector.
    const float minX = decal->hasWorldRect ? decal->worldMinX : -1.0e9f;
    const float minZ = decal->hasWorldRect ? decal->worldMinZ : -1.0e9f;
    const float maxX = decal->hasWorldRect ? decal->worldMaxX : 1.0e9f;
    const float maxZ = decal->hasWorldRect ? decal->worldMaxZ : 1.0e9f;

    lottex::RemovedLotTexture removed;
    if (!lottex::RemoveLotTexture(city_, decal->position.fX, decal->position.fZ,
                                  decal->instanceId, minX, minZ, maxX, maxZ, removed)) {
        return false;
    }
    if (onStripAdded_) {
        onStripAdded_(removed.record);
    }
    undoStack_.push_back(std::move(removed));
    LOG_INFO("DecalStripperInputControl: removed lot texture 0x{:08X}, {} undo(s) available",
             decal->instanceId, undoStack_.size());
    return true;
}

void DecalStripperInputControl::DeleteDecalsInBrush_() {
    if (!cursorValid_ || !decalService_) {
        return;
    }

    const uint32_t count = decalService_->GetDecalCount();
    if (count == 0) {
        return;
    }

    std::vector<TerrainDecalSnapshot> snapshots(count);
    const uint32_t copied = decalService_->CopyDecals(
        snapshots.data(),
        count,
        static_cast<uint32_t>(sizeof(TerrainDecalSnapshot)));

    const float radiusSq = kBrushRadiusMeters * kBrushRadiusMeters;
    size_t removedCount = 0;

    for (uint32_t i = 0; i < copied; ++i) {
        const cS3DVector2& center = snapshots[i].state.decalInfo.center;
        const float dx = center.fX - currentCursorWorld_.fX;
        const float dz = center.fY - currentCursorWorld_.fZ;
        if (dx * dx + dz * dz <= radiusSq) {
            if (decalService_->RemoveDecal(snapshots[i].id)) {
                undoStack_.push_back(DeletedDecalInfo{snapshots[i].state});
                ++removedCount;
            }
        }
    }

    if (removedCount > 0) {
        LOG_INFO("DecalStripperInputControl: brush removed {} decal(s)", removedCount);
    }
}

void DecalStripperInputControl::DrawOverlay(IDirect3DDevice7* device) {
    if (!device) {
        return;
    }
    overlay_.Draw(device, false);
}

void DecalStripperInputControl::BuildOverlay_() {
    if (!cursorValid_) {
        overlay_.Clear();
        return;
    }

    const float radius = (stripMode_ == StripMode::Brush) ? kBrushRadiusMeters : kPickRadiusMeters;
    overlay_.BuildStripperPreview(true, currentCursorWorld_, radius, GetTerrain_());

    // Outline the hovered lot texture's footprint so the selection is visible.
    if (stripMode_ == StripMode::Single && hoveredResult_) {
        if (const auto* decal = std::get_if<PickedDecal>(&*hoveredResult_);
            decal != nullptr && decal->hasWorldRect) {
            overlay_.AddRectOutline(decal->worldMinX, decal->worldMinZ,
                                    decal->worldMaxX, decal->worldMaxZ,
                                    GetTerrain_(), kRectHighlightColor);
        }
    }
}

cISTETerrain* DecalStripperInputControl::GetTerrain_() const {
    if (!city_) {
        return nullptr;
    }
    return city_->GetTerrain();
}
