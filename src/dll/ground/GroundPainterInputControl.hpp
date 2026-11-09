#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "../paint/PaintOverlay.hpp"
#include "GroundPaintDocument.hpp"
#include "GroundSnapping.hpp"
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
    // Lengths, turns and areas of what is being drawn or is selected; empty when there are none.
    [[nodiscard]] const std::string& MeasureText() const;

    // The next click on painted ground copies its look (and wear) into the tool's settings
    // instead of painting. Alt+click does the same without arming it.
    void ArmLookPick() { lookPickArmed_ = true; }
    [[nodiscard]] bool LookPickArmed() const { return lookPickArmed_; }
    // Goes up by one with every look copied, so the panel can take it over.
    [[nodiscard]] uint32_t LookPickCount() const { return lookPickCount_; }
    [[nodiscard]] size_t AnchorCount() const { return anchors_.size(); }
    // The point of the selected object that was clicked last, if it is still there.
    [[nodiscard]] std::optional<size_t> PickedPoint() const;

private:
    bool UpdateCursor_(int32_t screenX, int32_t screenZ);
    // Whether clicks place points that should snap: shapes drawn by clicking, and dragged points.
    [[nodiscard]] bool SnapsPoints_() const;
    // The point the next one is measured and lined up from, if any.
    [[nodiscard]] std::optional<Ground::Vec2> LastPoint_() const;
    // Moves cursor_ onto what is painted, a 15 degree step (Shift) or the grid, and remembers what
    // it snapped to for the preview.
    void SnapCursor_(int32_t screenX, int32_t screenZ);
    // Makes the snap index again if the painting changed since it was made.
    void UpdateSnapIndex_();
    void UpdateMeasure_();
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
    // Shift+drag in Select: moves the flat textures of the selection, not the shapes.
    bool StartTextureDrag_();
    void FinishTextureDrag_();
    // The selection with its textures moved as far as the mouse since the drag started.
    [[nodiscard]] std::vector<Ground::PaintObject> TextureDragged_() const;
    // Joins a brush stroke to the topmost brush paint painted the same way that it overlaps.
    bool JoinStroke_(const Ground::PaintObject& stroke);
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
    [[nodiscard]] bool UsesPen_() const;
    // How near the cursor must be to take a point: a few pixels on screen at any zoom.
    [[nodiscard]] float PickDistance_() const;
    [[nodiscard]] float CloseDistance_() const;
    [[nodiscard]] bool DrawsCurve_() const;
    // The path of the shape being drawn, up to the cursor.
    [[nodiscard]] std::vector<Ground::Vec2> DrawingPath_() const;
    [[nodiscard]] bool ClosesShape_() const;
    void RefreshPreview_();
    // Shows what mouse moves since the last tick changed.
    void ShowMove_();
    [[nodiscard]] cISTETerrain* Terrain_() const;

    // Snapping: the index of what is painted and the revision it was made for, the object left
    // out of it (whose point is being dragged), what the cursor snapped to, how far a snap
    // reaches (a few pixels, in metres at the current zoom), and the direction of the last edge
    // snapped to, to line up with.
    Ground::SnapIndex snapIndex_{};
    uint64_t snapRevision_ = 0;
    uint32_t snapLeftOut_ = 0;
    std::optional<Ground::SnapResult> snap_{};
    float snapReach_ = 1.5f;
    std::optional<Ground::Vec2> alignDirection_{};
    std::string measure_{};
    // The selection's measure, worked out again only when it or the painting changes.
    mutable std::string selectionMeasure_{};
    mutable uint32_t selectionMeasureId_ = 0;
    mutable uint64_t selectionMeasureRevision_ = 0;

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
    // Pen: the handle of each anchor, and pulling out the handle of the last one.
    std::vector<Ground::Vec2> anchorHandles_{};
    bool penDrag_ = false;
    Ground::Vec2 penHandleBefore_{};
    int32_t penPressX_ = 0;
    int32_t penPressZ_ = 0;
    // Freehand lines, brush and eraser strokes while the left button is down.
    bool strokeActive_ = false;
    std::vector<Ground::Vec2> stroke_{};
    // Moving the selection, or its textures.
    bool dragging_ = false;
    bool textureDrag_ = false;
    Ground::Vec2 dragStart_{};
    // Dragging one point of the selected object: which, and the object as it is being changed.
    std::optional<size_t> pointDrag_{};
    // Or one end of a point's curve handle: the point, and which end.
    std::optional<size_t> handleDrag_{};
    float handleSign_ = 1.0f;
    uint32_t pickedObject_ = 0;
    size_t pickedIndex_ = 0;
    Ground::PaintObject pointEdit_{};
    // Middle-button drags: changing the brush radius, or turning (Shift: scaling) the selection.
    enum class MiddleDrag : uint8_t { None, Brush, Turn, Scale };
    MiddleDrag middleDrag_ = MiddleDrag::None;
    int32_t middleStartX_ = 0;
    float middleStartRadius_ = 0.0f;
    Ground::Vec2 middlePivot_{};
    float middleAmount_ = 0.0f;
};
