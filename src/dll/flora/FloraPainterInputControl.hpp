#pragma once
#include <string>

#include "../../shared/entities.hpp"
#include "../common/BasePainterInputControl.hpp"
#include "cISC4FloraOccupant.h"
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
    [[nodiscard]] bool ShouldShowModelPreview_() const override;
    [[nodiscard]] bool HasActivePreviewOccupant_() const override;
    void CreatePreviewOccupant_() override;
    void DestroyPreviewOccupant_() override;
    void HidePreviewForPick_() override;
    void UpdatePreviewOccupantRotation_() override;
    void UpdatePreviewOccupant_() override;
    void PopulatePreviewBounds_(PaintOverlay::PreviewPlacement& placement, uint32_t typeID) const override;

private:
    cRZAutoRefCount<cISC4FloraSimulator> floraSimulator_;
    const FloraRepository* floraRepository_{nullptr};

    cRZAutoRefCount<cISC4FloraOccupant> previewFlora_{};
    cRZAutoRefCount<cISC4Occupant> previewOccupant_{};
};
