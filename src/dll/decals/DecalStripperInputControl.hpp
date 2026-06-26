#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include "LotTextureStripper.hpp"
#include "cISC4City.h"
#include "cRZAutoRefCount.h"
#include "cS3DVector3.h"
#include "cSC4BaseViewInputControl.h"
#include "../paint/PaintOverlay.hpp"
#include "../pick/ScenePickResult.hpp"
#include "../pick/ScenePickStrategy.hpp"
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
    bool OnMouseWheel(int32_t x, int32_t z, uint32_t modifiers, int32_t wheelDelta) override;
    bool OnKeyDown(int32_t vkCode, uint32_t modifiers) override;

    void Activate() override;
    void Deactivate() override;

    void SetDecalService(cIGZTerrainDecalService* service);
    void SetCity(cISC4City* pCity);
    // The strategy drives single-mode hover/pick: it surfaces both terrain
    // decals and lot base/overlay textures, with Alt+wheel candidate cycling.
    void SetPickStrategy(std::unique_ptr<ScenePickStrategy> strategy);
    void SetOnCancel(std::function<void()> onCancel);
    // Reports lot-texture strips/undos so the director can persist them into the
    // city save; lot textures regenerate from config on load and must be redone.
    void SetStripPersistence(std::function<void(const lottex::StripRecord&)> onAdded,
                             std::function<void(const lottex::StripRecord&)> onRemoved);
    void UndoLastDeletion();
    void ProcessPendingActions();
    void DrawOverlay(IDirect3DDevice7* device);

private:
    enum class StripMode { Single, Brush };

    bool UpdateCursorWorldFromScreen_(int32_t screenX, int32_t screenZ);

    // Single mode: refresh the hovered candidate from the pick strategy.
    void RefreshHover_();
    void ClearHover_();
    // Acts on the current hovered candidate (decal -> decal service; lot texture
    // -> occupant edit). Returns true if something was removed.
    bool DeleteHovered_();

    // Brush mode (terrain decals only): radius delete around the cursor.
    void DeleteDecalsInBrush_();

    void BuildOverlay_();

    struct DeletedDecalInfo {
        TerrainDecalState state{};  // Full state for reconstruction via CreateDecal
    };
    using UndoEntry = std::variant<DeletedDecalInfo, lottex::RemovedLotTexture>;

    [[nodiscard]] cISTETerrain* GetTerrain_() const;

    cRZAutoRefCount<cISC4City>          city_;
    cIGZTerrainDecalService*            decalService_{nullptr};
    std::unique_ptr<ScenePickStrategy>  pickStrategy_{};
    bool                                active_{false};
    bool                                cancelPending_{false};
    bool                                leftMouseDown_{false};
    StripMode                           stripMode_{StripMode::Single};
    cS3DVector3                         currentCursorWorld_{};
    bool                                cursorValid_{false};
    std::optional<ScenePickResult>      hoveredResult_{};

    std::function<void()>               onCancel_;
    std::function<void(const lottex::StripRecord&)> onStripAdded_;
    std::function<void(const lottex::StripRecord&)> onStripRemoved_;
    std::vector<UndoEntry>              undoStack_;
    PaintOverlay                        overlay_{};
};
