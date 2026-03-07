#pragma once
#include <functional>
#include <vector>

#include "../paint/PaintOverlay.hpp"
#include "cISC4City.h"
#include "cISC4Occupant.h"
#include "cISC4PropManager.h"
#include "cRZAutoRefCount.h"
#include "cS3DVector3.h"
#include "cSC4BaseViewInputControl.h"

class cISTETerrain;

class PropStripperInputControl : public cSC4BaseViewInputControl {
public:
    PropStripperInputControl();
    ~PropStripperInputControl() override;

    bool Init() override;
    bool Shutdown() override;

    bool OnMouseDownL(int32_t x, int32_t z, uint32_t modifiers) override;
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
    bool UpdateCursorWorldFromScreen_(int32_t screenX, int32_t screenZ);
    void PickNearestProp_();
    void SetHoveredProp_(cISC4Occupant* occupant);
    void ClearHoveredProp_();
    void DeleteHoveredProp_();
    void BuildOverlay_();
    [[nodiscard]] cISTETerrain* GetTerrain_() const;

    struct DeletedPropInfo {
        uint32_t propType = 0;
        cS3DVector3 position{};
        int32_t orientation = 0;
    };

    cRZAutoRefCount<cISC4City> city_;
    cRZAutoRefCount<cISC4PropManager> propManager_;
    cRZAutoRefCount<cISC4Occupant> hoveredOccupant_;

    bool active_ = false;
    bool cancelPending_ = false;
    cS3DVector3 currentCursorWorld_{};
    bool cursorValid_ = false;

    std::function<void()> onCancel_;
    std::vector<DeletedPropInfo> undoStack_;
    PaintOverlay overlay_{};
};
