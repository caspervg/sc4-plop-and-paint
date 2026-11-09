#include "DecalStripperInputControl.hpp"

#include <windows.h>

#include <limits>

#include "cISTETerrain.h"
#include "DecalHit.hpp"
#include "../utils/Logger.h"

namespace {
    constexpr auto  kDecalStripperControlID = 0x6E5D4C3Bu;
    constexpr float kBrushRadiusMeters      = 16.0f;
    constexpr size_t kMaxUndoGroups         = 50;
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
    active_ = false;
    overlay_.SetWorldDrawService(nullptr);
    if (!initialized) {
        return true;
    }
    ClearHoveredDecal_();
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
        brushGroupOpen_ = false;
        if (!cursorValid_) {
            return false;
        }
        DeleteDecalsInBrush_();
        BuildOverlay_();
        return true;
    }
    if (!cursorValid_ || !hasHoveredDecal_) {
        return false;
    }
    DeleteHoveredDecal_();
    PickNearestDecalForHover_();  // the next click needs no mouse move
    return true;
}

bool DecalStripperInputControl::OnMouseUpL(const int32_t /*x*/, const int32_t /*z*/, const uint32_t /*modifiers*/) {
    leftMouseDown_ = false;
    return active_ && IsOnTop();
}

bool DecalStripperInputControl::OnMouseMove(const int32_t x, const int32_t z, const uint32_t /*modifiers*/) {
    if (!active_ || !IsOnTop()) {
        return false;
    }
    UpdateCursorWorldFromScreen_(x, z);
    if (stripMode_ == StripMode::Brush) {
        ClearHoveredDecal_();
        if (leftMouseDown_) {
            DeleteDecalsInBrush_();
        }
    }
    else {
        PickNearestDecalForHover_();
    }
    BuildOverlay_();
    return true;
}

bool DecalStripperInputControl::OnKeyDown(const int32_t vkCode, const uint32_t modifiers) {
    if (!active_ || !IsOnTop()) {
        return false;
    }

    if (vkCode == VK_ESCAPE) {
        leftMouseDown_ = false;
        ClearHoveredDecal_();
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
        if (stripMode_ == StripMode::Brush) {
            ClearHoveredDecal_();
            LOG_INFO("DecalStripperInputControl: switched to brush mode");
        }
        else {
            PickNearestDecalForHover_();
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
    // The game also deactivates a control it covers, e.g. with its right-drag camera pan, and
    // does not activate it again when that ends, so active_ stays set until Shutdown.
    leftMouseDown_ = false;
    ProcessPendingActions();
    ClearHoveredDecal_();
    overlay_.Clear();
    cSC4BaseViewInputControl::Deactivate();
    LOG_INFO("DecalStripperInputControl deactivated");
}

void DecalStripperInputControl::SetDecalService(cIGZTerrainDecalService* service) {
    decalService_ = service;
    if (!service) {
        ClearHoveredDecal_();
        undoStack_.clear();
    }
}

void DecalStripperInputControl::SetCity(cISC4City* pCity) {
    city_ = pCity;
}

void DecalStripperInputControl::SetOnCancel(std::function<void()> onCancel) {
    onCancel_ = std::move(onCancel);
}

void DecalStripperInputControl::UndoLastDeletion() {
    if (undoStack_.empty()) {
        LOG_DEBUG("DecalStripperInputControl: nothing to undo");
        return;
    }

    if (!decalService_) {
        LOG_WARN("DecalStripperInputControl: no service for undo; clearing history");
        undoStack_.clear();
        return;
    }

    const std::vector<TerrainDecalState> group = std::move(undoStack_.back());
    undoStack_.pop_back();
    brushGroupOpen_ = false;

    for (const TerrainDecalState& state : group) {
        TerrainDecalId newId{};
        if (!decalService_->CreateDecal(&state, static_cast<uint32_t>(sizeof(state)), &newId)) {
            LOG_WARN("DecalStripperInputControl: failed to recreate decal during undo");
        }
    }

    LOG_INFO("DecalStripperInputControl: restored {} decal(s), {} undo(s) remaining", group.size(), undoStack_.size());
}

void DecalStripperInputControl::TrimUndoStack_() {
    if (undoStack_.size() > kMaxUndoGroups) {
        undoStack_.erase(undoStack_.begin());
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

TerrainDecalId DecalStripperInputControl::PickNearestDecal_() const {
    if (!cursorValid_ || !decalService_) {
        return TerrainDecalId{};
    }

    const uint32_t count = decalService_->GetDecalCount();
    if (count == 0) {
        return TerrainDecalId{};
    }

    std::vector<TerrainDecalSnapshot> snapshots(count);
    const uint32_t copied = decalService_->CopyDecals(
        snapshots.data(),
        count,
        static_cast<uint32_t>(sizeof(TerrainDecalSnapshot)));

    TerrainDecalId best{};
    float bestScore = std::numeric_limits<float>::max();

    for (uint32_t i = 0; i < copied; ++i) {
        const auto score = decals::MeasureDecalHit(snapshots[i].state, currentCursorWorld_.fX, currentCursorWorld_.fZ);
        if (score && *score < bestScore) {
            bestScore = *score;
            best = snapshots[i].id;
        }
    }

    return best;
}

void DecalStripperInputControl::PickNearestDecalForHover_() {
    const TerrainDecalId id = PickNearestDecal_();
    SetHoveredDecal_(id);
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
                if (!brushGroupOpen_) {
                    undoStack_.emplace_back();
                    brushGroupOpen_ = true;
                    TrimUndoStack_();
                }
                undoStack_.back().push_back(snapshots[i].state);
                ++removedCount;
            }
        }
    }

    if (removedCount > 0) {
        LOG_INFO("DecalStripperInputControl: brush removed {} decal(s)", removedCount);
    }
}

void DecalStripperInputControl::SetHoveredDecal_(const TerrainDecalId id) {
    if (hasHoveredDecal_ && hoveredDecalId_.value == id.value) {
        return;
    }

    // Restore previous hovered decal's drawMode
    if (hasHoveredDecal_ && decalService_) {
        TerrainDecalSnapshot snap{};
        if (decalService_->GetDecal(hoveredDecalId_, &snap, static_cast<uint32_t>(sizeof(snap)))) {
            snap.state.drawMode = hoveredOriginalDrawMode_;
            decalService_->ReplaceDecal(hoveredDecalId_, &snap.state, static_cast<uint32_t>(sizeof(snap.state)));
        }
    }

    hasHoveredDecal_ = (id.value != 0);
    hoveredDecalId_  = id;
    hoveredOriginalDrawMode_ = 0;

    // Highlight new hovered decal
    if (hasHoveredDecal_ && decalService_) {
        TerrainDecalSnapshot snap{};
        if (decalService_->GetDecal(id, &snap, static_cast<uint32_t>(sizeof(snap)))) {
            hoveredOriginalDrawMode_ = snap.state.drawMode;
            snap.state.drawMode = 1;
            decalService_->ReplaceDecal(id, &snap.state, static_cast<uint32_t>(sizeof(snap.state)));
        }
    }
}

void DecalStripperInputControl::ClearHoveredDecal_() {
    SetHoveredDecal_(TerrainDecalId{});
}

void DecalStripperInputControl::DeleteHoveredDecal_() {
    if (!hasHoveredDecal_ || !decalService_) {
        return;
    }

    TerrainDecalSnapshot snap{};
    if (!decalService_->GetDecal(hoveredDecalId_, &snap, static_cast<uint32_t>(sizeof(snap)))) {
        LOG_WARN("DecalStripperInputControl: failed to get decal before removal");
        hasHoveredDecal_ = false;
        return;
    }

    // Restore drawMode before storing for undo
    snap.state.drawMode = hoveredOriginalDrawMode_;

    if (!decalService_->RemoveDecal(hoveredDecalId_)) {
        LOG_WARN("DecalStripperInputControl: failed to remove decal id={}", hoveredDecalId_.value);
        hasHoveredDecal_ = false;
        return;
    }

    undoStack_.push_back({snap.state});
    TrimUndoStack_();
    LOG_INFO("DecalStripperInputControl: removed decal id={}, {} undo(s) available",
             hoveredDecalId_.value, undoStack_.size());

    hasHoveredDecal_ = false;
    hoveredDecalId_  = {};
}

void DecalStripperInputControl::SetWorldDrawService(cIGZWorldDrawService* service) {
    overlay_.SetWorldDrawService(service);
}

void DecalStripperInputControl::BuildOverlay_() {
    if (!cursorValid_) {
        overlay_.Clear();
        return;
    }

    const float radius = (stripMode_ == StripMode::Brush) ? kBrushRadiusMeters : decals::kPickRadiusMeters;
    overlay_.BuildStripperPreview(true, currentCursorWorld_, radius, GetTerrain_());
}

cISTETerrain* DecalStripperInputControl::GetTerrain_() const {
    if (!city_) {
        return nullptr;
    }
    return city_->GetTerrain();
}
