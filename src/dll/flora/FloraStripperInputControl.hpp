#pragma once
#include <functional>
#include <vector>

#include "cISC4City.h"
#include "cISC4FloraSimulator.h"
#include "cISC4Occupant.h"
#include "cISC4OccupantManager.h"
#include "cRZAutoRefCount.h"
#include "cS3DVector3.h"
#include "cSC4BaseViewInputControl.h"
#include "../paint/PaintOverlay.hpp"

class cISTETerrain;

class FloraStripperInputControl : public cSC4BaseViewInputControl {
public:
    FloraStripperInputControl();
    ~FloraStripperInputControl() override;

    bool Init() override;
    bool Shutdown() override;

    bool OnMouseDownL(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseUpL(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseDownR(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseMove(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnKeyDown(int32_t vkCode, uint32_t modifiers) override;

    void Activate() override;
    void Deactivate() override;

    void SetCity(cISC4City* pCity);
    void SetOnCancel(std::function<void()> onCancel);
    void UndoLastDeletion();
    void ProcessPendingActions();
    void DrawOverlay(IDirect3DDevice7* device);

private:
    enum class StripMode {
        Single,
        Brush
    };

    bool UpdateCursorWorldFromScreen_(int32_t screenX, int32_t screenZ);
    void BuildSearchBounds_(float minBounds[3], float maxBounds[3]) const;
    void PickNearestFlora_();
    void DeleteFloraInBrush_();
    void SetHoveredFlora_(cISC4Occupant* occupant);
    void ClearHoveredFlora_();
    void DeleteHoveredFlora_();
    void BuildOverlay_();
    [[nodiscard]] cISTETerrain* GetTerrain_() const;

    struct DeletedFloraInfo {
        uint32_t floraType = 0;
        cS3DVector3 position{};
        int32_t orientation = 0;
    };

    cRZAutoRefCount<cISC4City> city_;
    cRZAutoRefCount<cISC4FloraSimulator> floraSimulator_;
    cRZAutoRefCount<cISC4OccupantManager> occupantManager_;
    cRZAutoRefCount<cISC4Occupant> hoveredOccupant_;

    bool active_ = false;
    bool cancelPending_ = false;
    bool leftMouseDown_ = false;
    StripMode stripMode_{StripMode::Single};
    cS3DVector3 currentCursorWorld_{};
    bool cursorValid_ = false;

    std::function<void()> onCancel_;
    std::vector<DeletedFloraInfo> undoStack_;
    PaintOverlay overlay_{};
};
