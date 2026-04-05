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
    enum SourceFlags : uint32_t {
        SourceFlagNone = 0,
        SourceFlagCity = 1u << 0,
        SourceFlagLot = 1u << 1,
        SourceFlagStreet = 1u << 2
    };

    enum class SourceKind {
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
    void SetEnabledSources(uint32_t sourceFlags);
    [[nodiscard]] uint32_t GetEnabledSources() const noexcept;
    [[nodiscard]] bool HasEnabledSource(SourceKind source) const noexcept;
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
    struct CollectedProp {
        cISC4Occupant* occupant = nullptr;
        SourceKind source = SourceKind::City;
        cS3DVector3 position{};
        uint32_t propType = 0;
        int32_t orientation = 0;
    };

    void AppendCandidateProps_(std::vector<CollectedProp>& candidates, SourceKind source) const;
    void CollectCandidateProps_(std::vector<CollectedProp>& candidates) const;
    bool TryRemoveProp_(cISC4Occupant* occupant, uint32_t propType, SourceKind source) const;
    void SetHoveredProp_(cISC4Occupant* occupant);
    void ClearHoveredProp_();
    void DeleteHoveredProp_();
    void BuildOverlay_();
    [[nodiscard]] cISTETerrain* GetTerrain_() const;

    struct DeletedPropInfo {
        SourceKind source = SourceKind::City;
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
    uint32_t enabledSources_{SourceFlagCity};
    StripMode stripMode_{StripMode::Single};
    cS3DVector3 currentCursorWorld_{};
    bool cursorValid_ = false;

    std::function<void()> onCancel_;
    std::vector<DeletedPropInfo> undoStack_;
    PaintOverlay overlay_{};
};
