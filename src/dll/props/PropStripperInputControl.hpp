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
    enum class TargetKind {
        City,
        Lot,
        Street
    };

    PropStripperInputControl();
    ~PropStripperInputControl() override;

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
    void SetTargetKind(TargetKind targetKind);
    [[nodiscard]] TargetKind GetTargetKind() const noexcept;
    void UndoLastDeletion();
    void ProcessPendingActions();
    void DrawOverlay(IDirect3DDevice7* device);

private:
    enum class StripMode {
        Single,
        Brush
    };

    bool UpdateCursorWorldFromScreen_(int32_t screenX, int32_t screenZ);
    bool TryGetCursorCell_(int& cellX, int& cellZ) const;
    void PickNearestProp_();
    void DeletePropsInBrush_();
    void CollectCandidateProps_(SC4List<cISC4Occupant*>& candidates) const;
    bool TryRemoveProp_(cISC4Occupant* occupant, uint32_t propType) const;
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
    bool leftMouseDown_ = false;
    TargetKind targetKind_{TargetKind::City};
    StripMode stripMode_{StripMode::Single};
    cS3DVector3 currentCursorWorld_{};
    bool cursorValid_ = false;

    std::function<void()> onCancel_;
    std::vector<DeletedPropInfo> undoStack_;
    PaintOverlay overlay_{};
};
