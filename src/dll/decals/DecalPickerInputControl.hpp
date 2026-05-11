#pragma once
#include <functional>

#include "cISC4City.h"
#include "cRZAutoRefCount.h"
#include "cS3DVector3.h"
#include "cSC4BaseViewInputControl.h"
#include "../paint/PaintOverlay.hpp"
#include "public/cIGZTerrainDecalService.h"

class cISTETerrain;

class DecalPickerInputControl : public cSC4BaseViewInputControl {
public:
    DecalPickerInputControl();
    ~DecalPickerInputControl() override = default;

    bool Init() override;
    bool Shutdown() override;

    bool OnMouseDownL(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseDownR(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseMove(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnKeyDown(int32_t vkCode, uint32_t modifiers) override;

    void Activate() override;
    void Deactivate() override;

    void SetDecalService(cIGZTerrainDecalService* service);
    void SetCity(cISC4City* pCity);
    void SetOnPick(std::function<void(uint32_t instanceId)> onPick);
    void SetOnCancel(std::function<void()> onCancel);
    void ProcessPendingActions();
    void DrawOverlay(IDirect3DDevice7* device);

private:
    bool UpdateCursorWorldFromScreen_(int32_t screenX, int32_t screenZ);
    [[nodiscard]] TerrainDecalId PickNearestDecal_() const;
    void SetHoveredDecal_(TerrainDecalId id);
    void ClearHoveredDecal_();
    void BuildOverlay_();
    [[nodiscard]] cISTETerrain* GetTerrain_() const;

    cRZAutoRefCount<cISC4City>    city_;
    cIGZTerrainDecalService*      decalService_{nullptr};
    bool                          active_{false};
    bool                          cancelPending_{false};
    bool                          pickPending_{false};
    uint32_t                      pickedInstanceId_{0};
    cS3DVector3                   currentCursorWorld_{};
    bool                          cursorValid_{false};
    TerrainDecalId                hoveredDecalId_{};
    bool                          hasHoveredDecal_{false};
    uint8_t                       hoveredOriginalDrawMode_{0};

    std::function<void(uint32_t)> onPick_;
    std::function<void()>         onCancel_;
    PaintOverlay                  overlay_{};
};
