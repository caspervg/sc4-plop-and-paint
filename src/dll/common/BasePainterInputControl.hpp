#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../../shared/entities.hpp"
#include "../paint/PaintOverlay.hpp"
#include "../paint/PaintSettings.hpp"
#include "cISC4City.h"
#include "cISC4Occupant.h"
#include "cRZAutoRefCount.h"
#include "cS3DVector3.h"
#include "cSC4BaseViewInputControl.h"
#include "public/cIGZS3DCameraService.h"

class WeightedPicker;

class BasePainterInputControl : public cSC4BaseViewInputControl {
public:
    explicit BasePainterInputControl(uint32_t controlID);
    ~BasePainterInputControl() override;

    bool Init() override;
    bool Shutdown() override;

    bool OnMouseDownL(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseDownR(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseMove(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnKeyDown(int32_t vkCode, uint32_t modifiers) override;

    void Activate() override;
    void Deactivate() override;

    void SetTypeToPaint(uint32_t typeID, const PropPaintSettings& settings, const std::string& name);
    void SetCity(cISC4City* pCity);
    void SetCameraService(cIGZS3DCameraService* cameraService);
    void SetOnCancel(std::function<void()> onCancel);
    void SetOnQuickSwap(std::function<void(size_t)> onQuickSwap);

    [[nodiscard]] const PropPaintSettings& GetSettings() const { return settings_; }
    [[nodiscard]] bool HasPendingSketch() const;
    [[nodiscard]] bool HasPendingCancel() const { return cancelPending_; }
    [[nodiscard]] bool HasPendingPlacements() const;
    [[nodiscard]] bool SupportsVerticalAdjustment() const;
    [[nodiscard]] float GetSketchCaptureOffsetMeters() const;
    [[nodiscard]] bool HasCustomSketchHeights() const;
    [[nodiscard]] bool HasCapturedDirectAbsoluteHeight() const;
    [[nodiscard]] float GetCapturedDirectAbsoluteHeight() const;
    [[nodiscard]] bool TryGetCursorSketchPreview(cS3DVector3& terrainPos, cS3DVector3& resolvedPos) const;
    [[nodiscard]] std::vector<cS3DVector3> GetResolvedCollectedPoints() const;
    [[nodiscard]] std::vector<cS3DVector3> GetCollectedPointTerrainAnchors() const;
    void ProcessPendingActions();
    void DrawOverlay(IDirect3DDevice7* device);

    virtual void UndoLastPlacement();
    virtual void CancelAllPlacements();
    virtual void CommitPlacements();

protected:
    struct CollectedPoint {
        cS3DVector3 terrainWorldPos;
        float capturedWorldY = 0.0f;
        float captureOffsetMeters = 0.0f;
    };

    // Places one item in the world
    virtual bool PlaceAtWorld_(const cS3DVector3& pos, int32_t rotation, uint32_t typeID) = 0;

    // Removes an occupant (undo)
    virtual void RemoveOccupant_(cISC4Occupant* occupant) = 0;

    // Called from SetCity() after city_ is updated; subclass acquires its manager here.
    virtual void OnCityChanged_(cISC4City* pCity) {}

    // Returns true if a 3D model preview should be shown in Direct mode.
    [[nodiscard]] virtual bool ShouldShowModelPreview_() const { return false; }
    [[nodiscard]] virtual bool SupportsVerticalAdjustment_() const { return true; }

    // Returns true if a preview occupant is currently alive in the city.
    [[nodiscard]] virtual bool HasActivePreviewOccupant_() const { return false; }

    // Creates the preview occupant; called when ShouldShowModelPreview_() is true
    // and HasActivePreviewOccupant_() is false.
    virtual void CreatePreviewOccupant_() {}

    // Destroys the preview occupant.
    virtual void DestroyPreviewOccupant_() {}

    // Temporarily hides the preview before a terrain pick so it doesn't interfere.
    // Called from UpdateCursorWorldFromScreen_ before PickTerrain.
    virtual void HidePreviewForPick_() {}

    // Updates the preview occupant's rotation.
    virtual void UpdatePreviewOccupantRotation_() {}

    // Moves the preview occupant to the current cursor position.
    virtual void UpdatePreviewOccupant_() {}

    // Populates the bounding-box fields of a PreviewPlacement for the overlay.
    virtual void PopulatePreviewBounds_(PaintOverlay::PreviewPlacement& placement,
                                        uint32_t typeID) const {}

    // Returns whether the current direct preview position is valid for placement.
    [[nodiscard]] virtual bool IsDirectPreviewPlacementValid_(const PlannedPaint& placement) const { return true; }

    // Forces the outline overlay to stay visible even in full-model-only mode.
    [[nodiscard]] virtual bool ShouldForceDirectOverlay_() const { return false; }


    // Called by PlaceAtWorld_() implementations to register the occupant in the undo stack.
    // When batchingPlacements_ is true, adds to the current group (for line/polygon).
    // When false, creates a single-item group immediately (for direct paint).
    void AddOccupantToUndo_(cISC4Occupant* occupant);

    [[nodiscard]] bool IsBatchingPlacements_() const { return batchingPlacements_; }

    [[nodiscard]] uint32_t CurrentDirectTypeID_() const;
    void AdvanceDirectPaintType_();
    void ResetDirectPaintPicker_();

    [[nodiscard]] cISTETerrain* GetTerrain_() const;
    [[nodiscard]] float GetGridStepMeters_() const;
    [[nodiscard]] cS3DVector3 SnapWorldToGrid_(const cS3DVector3& position) const;
    [[nodiscard]] cS3DVector3 SnapXZToGridPreserveY_(const cS3DVector3& position) const;
    void SnapPlacementToGrid_(PlannedPaint& placement) const;
    void ClearCollectedPoints_();
    [[nodiscard]] cS3DVector3 ResolveSketchPoint_(const CollectedPoint& point) const;
    [[nodiscard]] cS3DVector3 ResolveCursorSketchPoint_() const;
    [[nodiscard]] bool ShouldUseCustomSketchHeights_() const;
    [[nodiscard]] float GetActiveVerticalOffset_() const;
    [[nodiscard]] cS3DVector3 ResolveDirectPosition_(const cS3DVector3& terrainPosition) const;
    void CaptureDirectAbsoluteHeight_();
    void ClearDirectAbsoluteHeight_();
    [[nodiscard]] virtual size_t PendingPlacementCount_() const;

    // Shared state
    cRZAutoRefCount<cISC4City> city_;
    cIGZS3DCameraService* cameraService_{nullptr};

    uint32_t typeToPaint_{0};
    uint32_t directPaintTypeID_{0};
    PropPaintSettings settings_{};
    std::unique_ptr<WeightedPicker> directPaintPicker_{};

    // Preview position/rotation tracking (used by SyncPreviewForState_ logic)
    uint32_t previewOccupantTypeID_{0};
    bool previewPositionValid_{false};
    cS3DVector3 lastPreviewPosition_{};
    int32_t lastPreviewRotation_{0};

    // Current cursor world position — available to preview update overrides.
    cS3DVector3 currentCursorWorld_{};
    bool cursorValid_{false};

    // Returns true when the state machine is in the ActiveDirect state.
    [[nodiscard]] bool IsInDirectPaintState_() const;

private:
    enum class ControlState {
        Uninitialized,
        ReadyNoTarget,
        ReadyWithTarget,
        ActiveNoTarget,
        ActiveDirect,
        ActiveLine,
        ActivePolygon,
    };

    [[nodiscard]] static bool IsActiveState_(ControlState state);
    [[nodiscard]] static bool IsTargetActiveState_(ControlState state);
    [[nodiscard]] static ControlState ActiveStateForMode_(PaintMode mode);
    [[nodiscard]] static const char* StateToString_(ControlState state);
    void TransitionTo_(ControlState newState, const char* reason);
    void SyncPreviewForState_();
    bool HandleActiveMouseDownL_(int32_t x, int32_t z, uint32_t modifiers);
    bool HandleActiveMouseMove_(int32_t x, int32_t z, uint32_t modifiers);
    bool HandleActiveKeyDown_(int32_t vkCode, uint32_t modifiers);
    bool UpdateCursorWorldFromScreen_(int32_t screenX, int32_t screenZ);
    void RebuildPreviewOverlay_();
    void ExecuteLinePlacement_();
    void ExecutePolygonPlacement_();
    bool PlaceTypeAt_(int32_t screenX, int32_t screenZ);

    void UndoLastPlacementInGroup_();
    void TrimUndoStack_();
    [[nodiscard]] bool ShouldShowOutlinePreview_() const;

    ControlState state_{ControlState::Uninitialized};

    std::function<void()> onCancel_{};
    std::function<void(size_t)> onQuickSwap_{};

    struct UndoGroup {
        std::vector<cRZAutoRefCount<cISC4Occupant>> props;
    };

    std::vector<UndoGroup> undoStack_{};
    UndoGroup currentUndoGroup_{};
    bool batchingPlacements_{false};

    std::vector<CollectedPoint> collectedPoints_{};

    PaintOverlay overlay_{};
    std::vector<PaintOverlay::PreviewPlacement> cachedPolygonPlacements_{};
    bool polygonPreviewDirty_{true};
    bool directAbsoluteHeightCaptured_{false};
    float directAbsoluteBaseY_{0.0f};

    bool cancelPending_{false};
};
