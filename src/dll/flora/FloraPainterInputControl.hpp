#pragma once
#include <string>

#include "../../shared/entities.hpp"
#include "../common/BasePainterInputControl.hpp"
#include "../paint/ModelPreview.hpp"
#include "cISC4FloraSimulator.h"
#include "cISC4Occupant.h"
#include "cRZAutoRefCount.h"

class FloraRepository;

class FloraPainterInputControl : public BasePainterInputControl {
public:
    FloraPainterInputControl();
    ~FloraPainterInputControl() override;

    void SetFloraToPaint(uint32_t floraTypeID, const PropPaintSettings& settings, const std::string& name);
    void SetFloraRepository(const FloraRepository* floraRepository);

protected:
    bool PlaceAtWorld_(const cS3DVector3& pos, int32_t rotation, uint32_t typeID) override;
    void RemoveOccupant_(cISC4Occupant* occupant) override;
    void OnCityChanged_(cISC4City* pCity) override;
    void OnDirectPlaced_(const cS3DVector3& position) override {
        // ponytail: with random rotation the game picks the facing, so the pop can face another way.
        const uint32_t typeID = CurrentDirectTypeID_();
        cursorPreview_.Pop(typeID, position, settings_.rotation, ItemSize_(typeID), ModelPreview::CurrentView(view3D));
    }
    [[nodiscard]] bool ShouldShowModelPreview_() const override;
    [[nodiscard]] bool HasActivePreviewOccupant_() const override;
    void CreatePreviewOccupant_() override;
    void DestroyPreviewOccupant_() override;
    void UpdatePreviewOccupant_() override;
    void PopulatePreviewBounds_(PaintOverlay::PreviewPlacement& placement, uint32_t typeID) const override;
    void ShowPlannedModels_(const std::vector<PaintOverlay::PreviewPlacement>& placements) override;
    [[nodiscard]] bool IsDirectPreviewPlacementValid_(const PlannedPaint& placement) const override;
    [[nodiscard]] bool ShouldForceDirectOverlay_() const override;
    [[nodiscard]] bool SupportsVerticalAdjustment_() const override { return false; }

private:
    [[nodiscard]] bool IsFloraPlacementValid_(uint32_t typeID, const cS3DVector3& position) const;
    void RefreshStaticFloraData_();

    cRZAutoRefCount<cISC4FloraSimulator> floraSimulator_;
    const FloraRepository* floraRepository_{nullptr};
    cSC4StaticFloraData* selectedFloraData_{nullptr};

    // The model under the cursor, and the ones planned along a line or in a polygon.
    ModelPreview cursorPreview_;
    ModelPreview plannedPreview_;
    bool cursorPreviewActive_{false};
};
