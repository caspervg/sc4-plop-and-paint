#pragma once
#include <string>

#include "../../shared/entities.hpp"
#include "../common/BasePainterInputControl.hpp"
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

    [[nodiscard]] bool ShouldShowModelPreview_() const override;
    [[nodiscard]] bool HasActivePreviewOccupant_() const override;
    void CreatePreviewOccupant_() override;
    void DestroyPreviewOccupant_() override;
    void HidePreviewForPick_() override;
    void UpdatePreviewOccupantRotation_() override;
    void UpdatePreviewOccupant_() override;
    void PopulatePreviewBounds_(PaintOverlay::PreviewPlacement& placement, uint32_t typeID) const override;

private:
    cRZAutoRefCount<cISC4PropManager> propManager_;
    const PropRepository* propRepository_{nullptr};

    cRZAutoRefCount<cISC4PropOccupant> previewProp_{};
    cRZAutoRefCount<cISC4Occupant> previewOccupant_{};
};
