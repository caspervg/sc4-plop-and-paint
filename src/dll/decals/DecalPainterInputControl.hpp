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

    // Decals have no 3D model preview.
    [[nodiscard]] bool ShouldShowModelPreview_() const override { return false; }
    [[nodiscard]] bool SupportsVerticalAdjustment_() const override { return false; }

    // Populate overlay bounds from baseSize so the outline circle scales correctly.
    void PopulatePreviewBounds_(PaintOverlay::PreviewPlacement& placement, uint32_t typeID) const override;

private:
    struct DecalUndoGroup {
        std::vector<TerrainDecalId> ids;
    };

    void AddDecalToUndo_(TerrainDecalId id);

    cIGZTerrainDecalService* decalService_{nullptr};
    uint32_t                 instanceId_{0};
    TerrainDecalState        stateTemplate_{};

    std::vector<DecalUndoGroup> undoStack_{};
    DecalUndoGroup              currentUndoGroup_{};
};
