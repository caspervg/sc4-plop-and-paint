#pragma once
#include <string>
#include <vector>

#include "../common/BasePainterInputControl.hpp"
#include "../paint/DecalPaintSettings.hpp"
#include "public/cIGZTerrainDecalService.h"

class DecalPainterInputControl : public BasePainterInputControl {
public:
    DecalPainterInputControl();
    ~DecalPainterInputControl() override = default;

    bool OnKeyDown(int32_t vkCode, uint32_t modifiers) override;

    void SetDecalToPaint(uint32_t instanceId, const DecalPaintSettings& settings, const std::string& name);
    void SetDecalService(cIGZTerrainDecalService* service);

    // Override undo/commit to manage TerrainDecalId instead of occupants.
    void UndoLastPlacement() override;
    void CancelAllPlacements() override;
    void CommitPlacements() override;

protected:
    // Places one decal via TerrainDecalService.
    bool PlaceAtWorld_(const cS3DVector3& pos, int32_t rotation, uint32_t typeID) override;

    // No occupant removal — decals are managed via TerrainDecalId.
    void RemoveOccupant_(cISC4Occupant* /*occupant*/) override {}

    [[nodiscard]] bool ShouldShowModelPreview_() const override;
    [[nodiscard]] bool SupportsVerticalAdjustment_() const override { return false; }
    [[nodiscard]] bool HasActivePreviewOccupant_() const override;
    void CreatePreviewOccupant_() override;
    void DestroyPreviewOccupant_() override;
    void UpdatePreviewOccupant_() override;

    // Populate overlay bounds from baseSize so the outline circle scales correctly.
    void PopulatePreviewBounds_(PaintOverlay::PreviewPlacement& placement, uint32_t typeID) const override;

private:
    struct PendingDecal {
        TerrainDecalId id{};
        uint8_t committedDrawMode{0};
    };

    struct DecalUndoGroup {
        std::vector<PendingDecal> decals;
    };

    void AddDecalToUndo_(TerrainDecalId id, uint8_t committedDrawMode);
    void TrimUndoStack_();
    void AdjustRotationDegrees_(float deltaDegrees);
    void RefreshPreviewDecal_();
    void RestoreCommittedDrawMode_(const PendingDecal& decal) const;
    void RestoreCommittedDrawMode_(const DecalUndoGroup& group) const;
    [[nodiscard]] TerrainDecalState BuildPreviewState_(const cS3DVector3& pos, uint32_t typeID) const;
    [[nodiscard]] size_t PendingPlacementCount_() const override;

    cIGZTerrainDecalService* decalService_{nullptr};
    uint32_t                 instanceId_{0};
    TerrainDecalState        stateTemplate_{};
    TerrainDecalId           previewDecalId_{};

    std::vector<DecalUndoGroup> undoStack_{};
    DecalUndoGroup              currentUndoGroup_{};
};
