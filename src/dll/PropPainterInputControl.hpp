#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../shared/entities.hpp"
#include "PropPaintOverlay.hpp"
#include "cISC4City.h"
#include "cISC4Occupant.h"
#include "cISC4PropManager.h"
#include "cRZAutoRefCount.h"
#include "cS3DVector3.h"
#include "cSC4BaseViewInputControl.h"
#include "public/cIGZS3DCameraService.h"

struct PropPainterPreviewState {
    bool cursorValid = false;
    cS3DVector3 cursorWorldPos;
    std::string propName;
    uint32_t propID = 0;
    int32_t rotation = 0;
    int32_t paintMode = 0;
};

enum class PropPaintMode {
    Direct = 0,
    Line = 1,
    Polygon = 2
};

enum class PropPreviewMode {
    Outline = 0,
    FullModel = 1,
    Combined = 2,
    Hidden = 3
};

struct PropPaintSettings {
    PropPaintMode mode = PropPaintMode::Direct;
    PropPreviewMode previewMode = PropPreviewMode::Outline;
    int32_t rotation = 0;
    float deltaYMeters = 0.0f;
    float spacingMeters = 5.0f;
    float densityPer100Sqm = 1.0f;
    float gridStepMeters = 16.0f;
    float randomOffset = 0.0f;
    bool alignToPath = false;
    bool randomRotation = false;
    bool showGrid = true;
    bool snapPointsToGrid = false;
    bool snapPlacementsToGrid = false;
    uint32_t randomSeed = 0;
    std::vector<FamilyEntry> activePalette{};
    float densityVariation = 0.0f;
};

class PropRepository;
class WeightedPropPicker;

class PropPainterInputControl : public cSC4BaseViewInputControl {
public:
    PropPainterInputControl();
    ~PropPainterInputControl() override;

    bool Init() override;
    bool Shutdown() override;

    bool OnMouseDownL(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseDownR(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseMove(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnKeyDown(int32_t vkCode, uint32_t modifiers) override;

    void Activate() override;
    void Deactivate() override;

    void SetPropToPaint(uint32_t propID, const PropPaintSettings& settings, const std::string& name);
    void SetCity(cISC4City* pCity);
    void SetCameraService(cIGZS3DCameraService* cameraService);
    void SetPropRepository(const PropRepository* propRepository);
    void SetOnCancel(std::function<void()> onCancel);

    [[nodiscard]] const PropPaintSettings& GetSettings() const { return settings_; }
    void ProcessPendingActions();
    void DrawOverlay(IDirect3DDevice7* device);

    void UndoLastPlacement();
    void CancelAllPlacements();
    void CommitPlacements();
    // void ShowPropPreview(const cS3DVector3& position, int32_t rotation);
    // void ClearPreview();

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
    [[nodiscard]] static ControlState ActiveStateForMode_(PropPaintMode mode);
    [[nodiscard]] static const char* StateToString_(ControlState state);
    void TransitionTo_(ControlState newState, const char* reason);
    void SyncPreviewForState_();
    bool HandleActiveMouseDownL_(int32_t x, int32_t z, uint32_t modifiers);
    bool HandleActiveMouseMove_(int32_t x, int32_t z, uint32_t modifiers);
    bool HandleActiveKeyDown_(int32_t vkCode, uint32_t modifiers);
    bool UpdateCursorWorldFromScreen_(int32_t screenX, int32_t screenZ);
    [[nodiscard]] float GetGridStepMeters_() const;
    [[nodiscard]] cS3DVector3 SnapWorldToGrid_(const cS3DVector3& position) const;
    void SnapPlacementToGrid_(PlannedProp& placement) const;
    void ClearCollectedPoints_();
    void ResetDirectPaintPicker_();
    [[nodiscard]] uint32_t CurrentDirectPropID_() const;
    void AdvanceDirectPaintProp_();
    void RebuildPreviewOverlay_();
    void ExecuteLinePlacement_();
    void ExecutePolygonPlacement_();

    void UndoLastPlacementInGroup();
    void TrimUndoStack_();
    bool PlacePropAt_(int32_t screenX, int32_t screenZ);
    bool PlacePropAtWorld_(const cS3DVector3& position, int32_t rotation, uint32_t propID);
    [[nodiscard]] size_t PendingPlacementCount_() const;
    [[nodiscard]] cISTETerrain* GetTerrain_() const;
    [[nodiscard]] bool ShouldShowOutlinePreview_() const;
    [[nodiscard]] bool ShouldShowModelPreview_() const;
    void CreatePreviewProp_();
    void DestroyPreviewProp_();
    void UpdatePreviewProp_();
    void UpdatePreviewPropRotation_();

    cRZAutoRefCount<cISC4City> city_;
    cRZAutoRefCount<cISC4PropManager> propManager_;
    ControlState state_ = ControlState::Uninitialized;

    uint32_t propIDToPaint_;
    uint32_t directPaintPropID_ = 0;
    PropPaintSettings settings_{};
    std::unique_ptr<WeightedPropPicker> directPaintPicker_{};
    cIGZS3DCameraService* cameraService_ = nullptr;
    const PropRepository* propRepository_ = nullptr;
    std::function<void()> onCancel_{};

    struct UndoGroup {
        std::vector<cRZAutoRefCount<cISC4Occupant>> props;
    };

    std::vector<UndoGroup> undoStack_;
    UndoGroup currentUndoGroup_{};
    bool batchingPlacements_ = false;
    struct CollectedPoint {
        cS3DVector3 worldPos;
    };
    std::vector<CollectedPoint> collectedPoints_{};
    cS3DVector3 currentCursorWorld_{};
    bool cursorValid_ = false;
    cRZAutoRefCount<cISC4PropOccupant> previewProp_{};
    cRZAutoRefCount<cISC4Occupant> previewOccupant_{};
    uint32_t previewPropID_ = 0;
    bool previewActive_ = false;
    bool previewPositionValid_ = false;
    cS3DVector3 lastPreviewPosition_{};
    int32_t lastPreviewRotation_ = 0;
    PropPaintOverlay overlay_{};
    std::vector<PropPaintOverlay::PreviewPlacement> cachedPolygonPlacements_{};
    bool polygonPreviewDirty_ = true;

    bool cancelPending_ = false;
};
