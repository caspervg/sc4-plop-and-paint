#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "../paint/PaintOverlay.hpp"
#include "GroundPaintDocument.hpp"
#include "GroundToolSettings.hpp"
#include "cISC4City.h"
#include "cRZAutoRefCount.h"
#include "cSC4BaseViewInputControlEx.h"

class cIGZWorldDrawService;
class cISTETerrain;

// The ground paint tools in the city view: draw areas, lines, brush strokes and road markings,
// rub them out, and pick, move, reshape and delete what was painted. What is drawn goes into a
// GroundPaintDocument.
//
// The middle and back/forward mouse buttons are shortcuts; every one of them has a key too.
class GroundPainterInputControl : public cSC4BaseViewInputControlEx {
public:
    GroundPainterInputControl();
    ~GroundPainterInputControl() override;

    bool Init() override;
    bool Shutdown() override;

    bool OnMouseDownL(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseUpL(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseMove(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnKeyDown(int32_t vkCode, uint32_t modifiers) override;
    bool OnMouseDownM(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseUpM(int32_t x, int32_t z, uint32_t modifiers) override;
    bool OnMouseDownX(int32_t x, int32_t z, uint32_t modifiers, uint32_t button) override;

    void Activate() override;
    void Deactivate() override;

    void SetCity(cISC4City* city);
    void SetDocument(Ground::GroundPaintDocument* document);
    void SetWorldDrawService(cIGZWorldDrawService* service);
    void SetSettings(const Ground::ToolSettings& settings);
    // The keys change the grid settings too.
    [[nodiscard]] const Ground::ToolSettings& Settings() const { return settings_; }
    void SetOnCancel(std::function<void()> onCancel);
    void ProcessPendingActions();
    // Pushed onto the view and not shut down since; it may be below a vanilla tool.
    [[nodiscard]] bool IsActive() const { return active_; }

    [[nodiscard]] bool IsDrawing() const { return !anchors_.empty() || strokeActive_; }
    // What to do next with the current tool, for the panel: one or two short sentences.
    [[nodiscard]] std::string StatusText() const;

    // The next click on painted ground copies its look (and wear) into the tool's settings
    // instead of painting. Alt+click does the same without arming it.
    void ArmLookPick() { lookPickArmed_ = true; }
    [[nodiscard]] bool LookPickArmed() const { return lookPickArmed_; }
    // Goes up by one with every look copied, so the panel can take it over.
    [[nodiscard]] uint32_t LookPickCount() const { return lookPickCount_; }
    [[nodiscard]] size_t AnchorCount() const { return anchors_.size(); }

private:
    bool UpdateCursor_(int32_t screenX, int32_t screenZ);
    void Commit_();
    void CancelShape_();
    void FinishStroke_();
    // Says in the log what a finished shape became, so "nothing shows" can be told apart from
    // "nothing was painted".
    void LogAdded_(const Ground::PaintObject& object, uint32_t id) const;
    void FinishDrag_();
    // The selected object whose points can be dragged, if exactly one is selected.
    [[nodiscard]] const Ground::PaintObject* PointEditTarget_() const;
    bool StartPointEdit_(bool insert, bool remove);
    void FinishPointDrag_();
    [[nodiscard]] size_t MarkingMaxPoints_() const;
    [[nodiscard]] bool UsesBrush_() const;
    void Undo_();
    void Redo_();
    void SetBrushRadius_(float radius);
    // Turns and scales the selection about its middle; preview only shows it.
    void TransformSelection_(float turnDegrees, float scale, bool preview);
    [[nodiscard]] Ground::PaintObject ObjectFrom_(const std::vector<Ground::Vec2>& points, bool curved) const;
    [[nodiscard]] std::vector<Ground::Vec2> PointsWithCursor_() const;
    [[nodiscard]] bool UsesStroke_() const;
    [[nodiscard]] bool ClosesShape_() const;
    void RefreshPreview_();
    // Shows what mouse moves since the last tick changed.
    void ShowMove_();
    [[nodiscard]] cISTETerrain* Terrain_() const;

    cRZAutoRefCount<cISC4City> city_{};
    Ground::GroundPaintDocument* document_ = nullptr;
    Ground::ToolSettings settings_{};
    std::function<void()> onCancel_{};
    PaintOverlay overlay_{};

    bool active_ = false;
    bool cancelPending_ = false;
    bool moved_ = false;
    bool lookPickArmed_ = false;
    uint32_t lookPickCount_ = 0;
    // Copies the look of the painted ground under the cursor; false when there is none.
    bool PickLook_();
    bool cursorValid_ = false;
    Ground::Vec2 cursor_{};
    std::vector<Ground::Vec2> anchors_{};
    // Freehand lines, brush and eraser strokes while the left button is down.
    bool strokeActive_ = false;
    std::vector<Ground::Vec2> stroke_{};
    // Moving the selection.
    bool dragging_ = false;
    Ground::Vec2 dragStart_{};
    // Dragging one point of the selected object: which, and the object as it is being changed.
    std::optional<size_t> pointDrag_{};
    Ground::PaintObject pointEdit_{};
    // Middle-button drags: changing the brush radius, or turning (Shift: scaling) the selection.
    enum class MiddleDrag : uint8_t { None, Brush, Turn, Scale };
    MiddleDrag middleDrag_ = MiddleDrag::None;
    int32_t middleStartX_ = 0;
    float middleStartRadius_ = 0.0f;
    Ground::Vec2 middlePivot_{};
    float middleAmount_ = 0.0f;
};
