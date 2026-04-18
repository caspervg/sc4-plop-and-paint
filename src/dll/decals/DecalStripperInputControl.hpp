#pragma once
#include <functional>
#include <vector>

#include "cISC4City.h"
#include "cRZAutoRefCount.h"
#include "cS3DVector3.h"
#include "cSC4BaseViewInputControl.h"
#include "../paint/PaintOverlay.hpp"
#include "public/cIGZTerrainDecalService.h"

class cISTETerrain;

class DecalStripperInputControl : public cSC4BaseViewInputControl {
public:
    DecalStripperInputControl();
    ~DecalStripperInputControl() override = default;

    bool Init() override;
    bool Shutdown() override;

    bool OnMouseDownL(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseUpL(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseDownR(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseMove(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnKeyDown(int32_t vkCode, uint32_t modifiers) override;

    void Activate() override;
    void Deactivate() override;

    void SetDecalService(cIGZTerrainDecalService* service);
    void SetCity(cISC4City* pCity);
    void SetOnCancel(std::function<void()> onCancel);
    void UndoLastDeletion();
    void ProcessPendingActions();
    void DrawOverlay(IDirect3DDevice7* device);

private:
    enum class StripMode { Single, Brush };

    bool UpdateCursorWorldFromScreen_(int32_t screenX, int32_t screenZ);

    // Returns the ID of the nearest decal within kPickRadiusMeters, or {0} if none.
    TerrainDecalId PickNearestDecal_() const;
    void PickNearestDecalForHover_();
    void DeleteDecalsInBrush_();
    void SetHoveredDecal_(TerrainDecalId id);
    void ClearHoveredDecal_();
    void DeleteHoveredDecal_();
    void BuildOverlay_();

    struct DeletedDecalInfo {
        TerrainDecalState state{};  // Full state for reconstruction via CreateDecal
    };

    [[nodiscard]] cISTETerrain* GetTerrain_() const;

    cRZAutoRefCount<cISC4City>    city_;
    cIGZTerrainDecalService*      decalService_{nullptr};
    bool                          active_{false};
    bool                          cancelPending_{false};
    bool                          leftMouseDown_{false};
    StripMode                     stripMode_{StripMode::Single};
    cS3DVector3                   currentCursorWorld_{};
    bool                          cursorValid_{false};
    TerrainDecalId                hoveredDecalId_{};
    bool                          hasHoveredDecal_{false};

    std::function<void()>         onCancel_;
    std::vector<DeletedDecalInfo> undoStack_;
    PaintOverlay                  overlay_{};
};
