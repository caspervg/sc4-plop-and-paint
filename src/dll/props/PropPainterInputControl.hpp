#pragma once
#include <string>

#include "../../shared/entities.hpp"
#include "../common/BasePainterInputControl.hpp"
#include "../paint/ModelPreview.hpp"
#include "../paint/PaintSettings.hpp"
#include "cISC4PropManager.h"
#include "cRZAutoRefCount.h"

class PropRepository;

class PropPainterInputControl : public BasePainterInputControl {
public:
    PropPainterInputControl();
    ~PropPainterInputControl() override;

    void SetPropToPaint(uint32_t propID, const PropPaintSettings& settings, const std::string& name);
    void SetPropRepository(const PropRepository* propRepository);

protected:
    bool PlaceAtWorld_(const cS3DVector3& pos, int32_t rotation, uint32_t typeID) override;
    void RemoveOccupant_(cISC4Occupant* occupant) override;

    void OnCityChanged_(cISC4City* pCity) override;
    void OnPlaced_(const cS3DVector3& position, const int32_t rotation, const uint32_t typeID) override {
        const uint32_t shownID = ResolveInSeasonPreviewProp_(typeID);
        cursorPreview_.Pop(shownID, position, rotation, ItemSize_(shownID), ModelPreview::CurrentView(view3D));
    }

    [[nodiscard]] bool ShouldShowModelPreview_() const override;
    [[nodiscard]] bool HasActivePreviewOccupant_() const override;
    [[nodiscard]] bool IsPreviewOccupantStale_() const override;
    void CreatePreviewOccupant_() override;
    void DestroyPreviewOccupant_() override;
    void UpdatePreviewOccupant_() override;
    void PopulatePreviewBounds_(PaintOverlay::PreviewPlacement& placement, uint32_t typeID) const override;
    void ShowPlannedModels_(const std::vector<PaintOverlay::PreviewPlacement>& placements) override;
    [[nodiscard]] bool ShouldForceDirectOverlay_() const override;

private:
    bool PlaceSingleProp_(uint32_t propToCreate, const cS3DVector3& pos, int32_t rotation);
    [[nodiscard]] uint32_t ResolveInSeasonPreviewProp_(uint32_t propID) const;

    cRZAutoRefCount<cISC4PropManager> propManager_;
    const PropRepository* propRepository_{nullptr};

    // The model under the cursor, and the ones planned along a line or in a polygon.
    ModelPreview cursorPreview_;
    ModelPreview plannedPreview_;
    bool cursorPreviewActive_{false};
    // Seasonal set member the preview shows (may differ from the selected previewOccupantTypeID_).
    uint32_t previewDisplayedPropID_{0};
};
