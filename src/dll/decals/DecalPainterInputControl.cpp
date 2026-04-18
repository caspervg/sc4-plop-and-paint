#include "DecalPainterInputControl.hpp"

#include <algorithm>
#include <cmath>

#include "cGZPersistResourceKey.h"
#include "../paint/PaintOverlay.hpp"
#include "../utils/Logger.h"

namespace {
    constexpr auto kDecalPainterControlID = 0x5D4C3B2Au;
    constexpr uint32_t kDecalTextureType  = 0x7AB50E44;
    constexpr uint32_t kDecalTextureGroup = 0x0986135E;
    constexpr size_t   kMaxUndoGroups     = 50;
    constexpr uint8_t  kPendingDrawMode   = 1;
}

DecalPainterInputControl::DecalPainterInputControl()
    : BasePainterInputControl(kDecalPainterControlID) {}

void DecalPainterInputControl::SetDecalToPaint(const uint32_t instanceId,
                                               const DecalPaintSettings& settings,
                                               const std::string& name) {
    instanceId_    = instanceId;
    stateTemplate_ = settings.stateTemplate;
    stateTemplate_.textureKey = cGZPersistResourceKey{kDecalTextureType, kDecalTextureGroup, instanceId};

    // Configure base-class state machine for Direct-mode-only painting.
    PropPaintSettings propSettings{};
    propSettings.mode        = PaintMode::Direct;
    propSettings.previewMode = PreviewMode::Outline;

    SetTypeToPaint(instanceId, propSettings, name);
}

void DecalPainterInputControl::SetDecalService(cIGZTerrainDecalService* service) {
    decalService_ = service;
}

bool DecalPainterInputControl::PlaceAtWorld_(const cS3DVector3& pos,
                                              const int32_t /*rotation*/,
                                              const uint32_t typeID) {
    if (!decalService_) {
        LOG_WARN("DecalPainterInputControl: no decal service available");
        return false;
    }

    TerrainDecalState state = stateTemplate_;
    state.textureKey.instance = typeID;
    state.decalInfo.center = cS3DVector2(pos.fX, pos.fZ);
    const uint8_t committedDrawMode = state.drawMode;
    state.drawMode = kPendingDrawMode;

    TerrainDecalId decalId{};
    if (!decalService_->CreateDecal(state, &decalId)) {
        LOG_WARN("DecalPainterInputControl: CreateDecal failed for 0x{:08X}", typeID);
        return false;
    }

    AddDecalToUndo_(decalId, committedDrawMode);
    LOG_DEBUG("DecalPainterInputControl: placed decal 0x{:08X} id={}", typeID, decalId.value);
    return true;
}

bool DecalPainterInputControl::ShouldShowModelPreview_() const {
    return settings_.previewMode != PreviewMode::Hidden && IsInDirectPaintState_();
}

bool DecalPainterInputControl::HasActivePreviewOccupant_() const {
    return previewDecalId_.value != 0;
}

void DecalPainterInputControl::CreatePreviewOccupant_() {
    if (!decalService_ || !cursorValid_ || previewDecalId_.value != 0) {
        return;
    }

    const cS3DVector3 worldPos = ResolveDirectPosition_(currentCursorWorld_);
    const uint32_t previewTypeID = CurrentDirectTypeID_();
    if (previewTypeID == 0) {
        return;
    }

    const TerrainDecalState previewState = BuildPreviewState_(worldPos, previewTypeID);

    TerrainDecalId decalId{};
    if (!decalService_->CreateDecal(previewState, &decalId)) {
        LOG_WARN("DecalPainterInputControl: failed to create temporary preview decal for 0x{:08X}", previewTypeID);
        return;
    }

    previewDecalId_ = decalId;
    previewOccupantTypeID_ = previewTypeID;
    lastPreviewPosition_ = worldPos;
    previewPositionValid_ = true;
}

void DecalPainterInputControl::DestroyPreviewOccupant_() {
    if (previewDecalId_.value != 0 && decalService_) {
        if (!decalService_->RemoveDecal(previewDecalId_)) {
            LOG_WARN("DecalPainterInputControl: failed to remove temporary preview decal id={}", previewDecalId_.value);
        }
    }

    previewDecalId_ = {};
    previewOccupantTypeID_ = 0;
    previewPositionValid_ = false;
}

void DecalPainterInputControl::UpdatePreviewOccupant_() {
    if (!decalService_) {
        DestroyPreviewOccupant_();
        return;
    }

    if (!cursorValid_) {
        DestroyPreviewOccupant_();
        return;
    }

    const cS3DVector3 worldPos = ResolveDirectPosition_(currentCursorWorld_);
    const uint32_t previewTypeID = CurrentDirectTypeID_();
    if (previewTypeID == 0) {
        DestroyPreviewOccupant_();
        return;
    }

    if (previewDecalId_.value == 0) {
        CreatePreviewOccupant_();
        return;
    }

    const bool posChanged =
        std::abs(worldPos.fX - lastPreviewPosition_.fX) > 0.05f ||
        std::abs(worldPos.fY - lastPreviewPosition_.fY) > 0.05f ||
        std::abs(worldPos.fZ - lastPreviewPosition_.fZ) > 0.05f;

    if (!posChanged && previewOccupantTypeID_ == previewTypeID) {
        previewPositionValid_ = true;
        return;
    }

    const TerrainDecalState previewState = BuildPreviewState_(worldPos, previewTypeID);
    if (!decalService_->ReplaceDecal(previewDecalId_, previewState)) {
        LOG_WARN("DecalPainterInputControl: failed to update temporary preview decal id={}", previewDecalId_.value);
        DestroyPreviewOccupant_();
        CreatePreviewOccupant_();
        return;
    }

    previewOccupantTypeID_ = previewTypeID;
    lastPreviewPosition_ = worldPos;
    previewPositionValid_ = true;
}

void DecalPainterInputControl::PopulatePreviewBounds_(PaintOverlay::PreviewPlacement& placement,
                                                       uint32_t /*typeID*/) const {
    const float aspectMultiplier = std::max(stateTemplate_.decalInfo.aspectMultiplier, 0.1f);
    placement.width = stateTemplate_.decalInfo.baseSize * aspectMultiplier;
    placement.depth = stateTemplate_.decalInfo.baseSize;
}

void DecalPainterInputControl::AddDecalToUndo_(const TerrainDecalId decalId, const uint8_t committedDrawMode) {
    PendingDecal pendingDecal{decalId, committedDrawMode};
    if (IsBatchingPlacements_()) {
        currentUndoGroup_.decals.push_back(pendingDecal);
    }
    else {
        DecalUndoGroup group;
        group.decals.push_back(pendingDecal);
        undoStack_.push_back(std::move(group));
        TrimUndoStack_();
    }
}

void DecalPainterInputControl::TrimUndoStack_() {
    while (undoStack_.size() > kMaxUndoGroups) {
        RestoreCommittedDrawMode_(undoStack_.front());
        undoStack_.erase(undoStack_.begin());
        LOG_DEBUG("DecalPainterInputControl: trimmed undo stack to {} group(s)", undoStack_.size());
    }
}

void DecalPainterInputControl::RestoreCommittedDrawMode_(const PendingDecal& decal) const {
    if (!decalService_ || decal.id.value == 0) {
        return;
    }

    TerrainDecalSnapshot snap{};
    if (!decalService_->GetDecal(decal.id, &snap)) {
        LOG_WARN("DecalPainterInputControl: failed to get decal id={} while restoring draw mode", decal.id.value);
        return;
    }

    if (snap.state.drawMode == decal.committedDrawMode) {
        return;
    }

    snap.state.drawMode = decal.committedDrawMode;
    if (!decalService_->ReplaceDecal(decal.id, snap.state)) {
        LOG_WARN("DecalPainterInputControl: failed to restore draw mode for decal id={}", decal.id.value);
    }
}

void DecalPainterInputControl::RestoreCommittedDrawMode_(const DecalUndoGroup& group) const {
    for (const PendingDecal& decal : group.decals) {
        RestoreCommittedDrawMode_(decal);
    }
}

void DecalPainterInputControl::UndoLastPlacement() {
    if (undoStack_.empty()) {
        LOG_DEBUG("DecalPainterInputControl: nothing to undo");
        return;
    }

    if (!decalService_) {
        LOG_WARN("DecalPainterInputControl: no service for undo; clearing stack");
        undoStack_.clear();
        return;
    }

    const DecalUndoGroup group = undoStack_.back();
    undoStack_.pop_back();

    for (const PendingDecal& decal : group.decals) {
        if (!decalService_->RemoveDecal(decal.id)) {
            LOG_WARN("DecalPainterInputControl: failed to remove decal id={} during undo", decal.id.value);
        }
    }
    LOG_INFO("DecalPainterInputControl: undone {} decal(s), {} group(s) remain",
             group.decals.size(), undoStack_.size());
}

void DecalPainterInputControl::CancelAllPlacements() {
    if (!decalService_) {
        undoStack_.clear();
        currentUndoGroup_.decals.clear();
        return;
    }

    for (const DecalUndoGroup& group : undoStack_) {
        for (const PendingDecal& decal : group.decals) {
            decalService_->RemoveDecal(decal.id);
        }
    }
    for (const PendingDecal& decal : currentUndoGroup_.decals) {
        decalService_->RemoveDecal(decal.id);
    }

    undoStack_.clear();
    currentUndoGroup_.decals.clear();
    LOG_INFO("DecalPainterInputControl: cancelled all placements");
}

void DecalPainterInputControl::CommitPlacements() {
    const size_t pendingCount = PendingPlacementCount_();
    LOG_INFO("DecalPainterInputControl: committing {} decal(s)", pendingCount);

    for (const DecalUndoGroup& group : undoStack_) {
        RestoreCommittedDrawMode_(group);
    }
    RestoreCommittedDrawMode_(currentUndoGroup_);

    undoStack_.clear();
    currentUndoGroup_.decals.clear();
}

TerrainDecalState DecalPainterInputControl::BuildPreviewState_(const cS3DVector3& pos, const uint32_t typeID) const {
    TerrainDecalState previewState = stateTemplate_;
    previewState.textureKey.instance = typeID;
    previewState.decalInfo.center = cS3DVector2(pos.fX, pos.fZ);
    previewState.drawMode = kPendingDrawMode;
    return previewState;
}

size_t DecalPainterInputControl::PendingPlacementCount_() const {
    size_t count = currentUndoGroup_.decals.size();
    for (const DecalUndoGroup& group : undoStack_) {
        count += group.decals.size();
    }
    return count;
}
