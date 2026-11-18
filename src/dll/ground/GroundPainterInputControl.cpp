#include "GroundPainterInputControl.hpp"

#include <algorithm>
#include <cmath>
#include <windows.h>

#include "cISTETerrain.h"
#include "../utils/Logger.h"

using namespace Ground;

namespace {
    constexpr auto kGroundPainterControlID = 0x7F6E5D52u;

    // Points closer than this to the first anchor close an area.
    constexpr float kCloseDistance = 1.0f;
    // Freehand points are recorded about this far apart, then simplified to this tolerance.
    constexpr float kFreehandSpacing = 0.5f;
    constexpr float kFreehandTolerance = 0.2f;
    // How close a click has to be to a point, or to the path between points, to pick it.
    constexpr float kPointPickDistance = 1.5f;
    constexpr float kPenDragPixels = 5.0f;
    // Middle-button drags: per pixel to the right, the brush grows this share, the selection
    // turns this many degrees or grows this share.
    constexpr float kBrushPerPixel = 0.01f;
    constexpr float kTurnPerPixel = 0.5f;
    constexpr float kScalePerPixel = 0.005f;
    // The same changes by key.
    constexpr float kBrushStep = 1.25f;
    constexpr float kTurnStep = 15.0f;
    constexpr float kFineTurnStep = 1.0f;
    constexpr float kScaleStep = 1.1f;
    // Where Ctrl+D puts the copies.
    constexpr Vec2 kDuplicateOffset{4.0f, 4.0f};
    // How far a snap reaches, on screen; and in metres when the terrain is not under that spot.
    constexpr int32_t kSnapPixels = 12;
    constexpr float kDefaultSnapReach = 1.5f;
    // Shift: points on 15 degree steps from the last. Lines within 2 degrees of parallel or square
    // to the last edge snapped to line up with it.
    constexpr float kAngleStep = 3.14159265f / 12.0f;
    constexpr float kAlignTolerance = 2.0f * 3.14159265f / 180.0f;
    constexpr float kRadiansToDegrees = 180.0f / 3.14159265f;

    constexpr uint32_t kPathColor = 0xE0FFFFFF;
    constexpr uint32_t kCursorPathColor = 0xC0FFD700;
    constexpr uint32_t kAnchorColor = 0xF0FFD700;
    constexpr uint32_t kEraserColor = 0xE0FF4040;
    constexpr uint32_t kSelectionColor = 0xF040C0FF;
    constexpr uint32_t kHoverColor = 0x9040C0FF;
    constexpr uint32_t kSnapPointColor = 0xF0FF40FF;
    constexpr uint32_t kSnapEdgeColor = 0xF040FFFF;
    constexpr uint32_t kSnapGuideColor = 0xA0FF40FF;

    bool KeyHeld(const int vk) {
        return (GetKeyState(vk) & 0x8000) != 0;
    }

    float Distance(const Vec2 a, const Vec2 b) {
        return std::hypot(a.x - b.x, a.z - b.z);
    }

    Vec2 ClosestOnSegment(const Vec2 p, const Vec2 a, const Vec2 b) {
        const float dx = b.x - a.x;
        const float dz = b.z - a.z;
        const float lengthSquared = dx * dx + dz * dz;
        const float t = lengthSquared > 0.0f ? std::clamp(((p.x - a.x) * dx + (p.z - a.z) * dz) / lengthSquared, 0.0f, 1.0f)
                                             : 0.0f;
        return {a.x + t * dx, a.z + t * dz};
    }

    float DistanceToSegment(const Vec2 p, const Vec2 a, const Vec2 b) {
        return Distance(p, ClosestOnSegment(p, a, b));
    }

    // Shape -1 is the object's own, 0 and up its holes.
    const ShapeSpec& ShapeOf(const PaintObject& object, const int shape) {
        return shape < 0 ? object.shape : object.erasers[static_cast<size_t>(shape)];
    }

    ShapeSpec& ShapeOf(PaintObject& object, const int shape) {
        return shape < 0 ? object.shape : object.erasers[static_cast<size_t>(shape)];
    }

    std::vector<int> EditableShapes(const PaintObject& object) {
        std::vector<int> shapes;
        if (HasEditablePoints(object)) {
            shapes.push_back(-1);
        }
        for (size_t i = 0; i < object.erasers.size(); ++i) {
            if (!object.erasers[i].points.empty()) {
                shapes.push_back(static_cast<int>(i));
            }
        }
        return shapes;
    }
}

GroundPainterInputControl::GroundPainterInputControl()
    : cSC4BaseViewInputControlEx(kGroundPainterControlID) {}

GroundPainterInputControl::~GroundPainterInputControl() = default;

bool GroundPainterInputControl::Init() {
    if (initialized) {
        return true;
    }
    return cSC4BaseViewInputControlEx::Init();
}

bool GroundPainterInputControl::Shutdown() {
    active_ = false;
    overlay_.SetWorldDrawService(nullptr);
    if (!initialized) {
        return true;
    }
    CancelShape_();
    overlay_.Clear();
    cSC4BaseViewInputControlEx::Shutdown();
    return true;
}

void GroundPainterInputControl::Activate() {
    cSC4BaseViewInputControlEx::Activate();
    if (!Init()) {
        LOG_WARN("GroundPainterInputControl: Init failed during Activate");
        return;
    }
    active_ = true;
}

// The game also deactivates a control it covers, e.g. with its right-drag camera pan, and does
// not activate it again when that ends. So this only pauses: the shape stays, Shutdown ends it.
// Gestures that hold a button down end here, since their button-up goes to the other control.
void GroundPainterInputControl::Deactivate() {
    if (Is_(Gesture::Stroke)) {
        CancelShape_();  // stroke tools never have anchors
    }
    if ((Is_(Gesture::MoveSelection) || Is_(Gesture::MoveTexture) || EditingPoints_()) && document_) {
        document_->ShowSelectionMoved({});
    }
    gesture_ = Gesture::None;
    RefreshPreview_();
    cSC4BaseViewInputControlEx::Deactivate();
}

void GroundPainterInputControl::SetCity(cISC4City* const city) {
    city_ = city;
}

void GroundPainterInputControl::SetDocument(GroundPaintDocument* const document) {
    document_ = document;
}

void GroundPainterInputControl::SetWorldDrawService(cIGZWorldDrawService* const service) {
    overlay_.SetWorldDrawService(service);
}

void GroundPainterInputControl::SetSettings(const ToolSettings& settings) {
    const bool toolChanged = settings.tool != settings_.tool || settings.drawStyle != settings_.drawStyle;
    settings_ = settings;
    if (toolChanged) {
        returnTool_.reset();
        CancelShape_();
    }
    RefreshPreview_();
}

void GroundPainterInputControl::SetOnCancel(std::function<void()> onCancel) {
    onCancel_ = std::move(onCancel);
}

void GroundPainterInputControl::ProcessPendingActions() {
    if (moved_ && active_) {
        ShowMove_();
    }
    else if (active_ && document_ && document_->Selection() != shownSelection_) {
        RefreshPreview_();  // selected in the panel
    }
    if (cancelPending_) {
        cancelPending_ = false;
        if (onCancel_) {
            onCancel_();
        }
    }
}

bool GroundPainterInputControl::UsesStroke_() const {
    switch (settings_.tool) {
    case Tool::Brush:
        return true;
    case Tool::Erase:
        return settings_.eraseWithBrush;
    case Tool::Area:
    case Tool::Line:
    case Tool::Path:
        return settings_.drawStyle == DrawStyle::Freehand;
    case Tool::Marking:
    case Tool::Select:
        return false;
    }
    return false;
}

bool GroundPainterInputControl::UsesPen_() const {
    return settings_.drawStyle == DrawStyle::Pen &&
           (settings_.tool == Tool::Area || DrawsLine(settings_.tool) ||
            (settings_.tool == Tool::Erase && !settings_.eraseWithBrush));
}

float GroundPainterInputControl::PickDistance_() const {
    return std::max(kPointPickDistance, snapReach_);
}

bool GroundPainterInputControl::WideLine_(const PaintObject& object) const {
    return object.shape.kind == ShapeKind::Line && 0.5f * object.shape.width > PickDistance_();
}

float GroundPainterInputControl::CloseDistance_() const {
    return UsesPen_() ? std::max(kCloseDistance, snapReach_) : kCloseDistance;
}

bool GroundPainterInputControl::DrawsCurve_() const {
    return settings_.drawStyle == DrawStyle::Curve || settings_.drawStyle == DrawStyle::Freehand;
}

std::vector<Vec2> GroundPainterInputControl::DrawingPath_() const {
    const std::vector<Vec2> points = PointsWithCursor_();
    if (UsesPen_()) {
        return PenPath(points, anchorHandles_, anchorInHandles_, {}, 0.0f, ClosesShape_());
    }
    return PathThrough(points, DrawsCurve_() ? PathStyle::Curved : PathStyle::Straight, ClosesShape_());
}

std::optional<size_t> GroundPainterInputControl::PickedPoint() const {
    const auto picked = Picked_();
    return picked && picked->first < 0 ? std::optional{picked->second} : std::nullopt;
}

std::optional<std::pair<int, size_t>> GroundPainterInputControl::Picked_() const {
    const PaintObject* const target = PointEditTarget_();
    if (!target || target->id != pickedObject_ || pickedShape_ >= static_cast<int>(target->erasers.size()) ||
        pickedIndex_ >= ShapeOf(*target, pickedShape_).points.size()) {
        return std::nullopt;
    }
    return std::pair{pickedShape_, pickedIndex_};
}

bool GroundPainterInputControl::ClosesShape_() const {
    return settings_.tool == Tool::Area || settings_.tool == Tool::Erase ||
           (settings_.tool == Tool::Marking && PointsFor(settings_.marking.kind).closed);
}

size_t GroundPainterInputControl::MarkingMaxPoints_() const {
    return settings_.tool == Tool::Marking ? PointsFor(settings_.marking.kind).maxPoints : 0;
}

bool GroundPainterInputControl::UpdateCursor_(const int32_t screenX, const int32_t screenZ) {
    float world[3] = {};
    cursorValid_ = view3D && view3D->PickTerrain(screenX, screenZ, world, false);
    if (!cursorValid_) {
        return false;
    }
    cursor_ = {world[0], world[2]};
    if (UsesPen_() || settings_.tool == Tool::Select || (SnapsPoints_() && settings_.snapToPaint)) {
        // A few pixels, in metres at this zoom: the terrain a little to the right.
        float aside[3] = {};
        snapReach_ = view3D && view3D->PickTerrain(screenX + kSnapPixels, screenZ, aside, false)
                         ? std::clamp(std::hypot(aside[0] - world[0], aside[2] - world[2]), 0.05f, 25.0f)
                         : kDefaultSnapReach;
    }
    SnapCursor_(screenX, screenZ);
    return true;
}

bool GroundPainterInputControl::SnapsPoints_() const {
    if (Is_(Gesture::PenHandle)) {
        return false;  // the handle goes where the mouse goes
    }
    if (Is_(Gesture::MovePoint)) {
        return true;
    }
    switch (settings_.tool) {
    case Tool::Area:
    case Tool::Line:
    case Tool::Path:
        return settings_.drawStyle != DrawStyle::Freehand;
    case Tool::Marking:
        return true;
    case Tool::Erase:
        return !settings_.eraseWithBrush && settings_.drawStyle != DrawStyle::Freehand;
    case Tool::Brush:
    case Tool::Select:
        return false;
    }
    return false;
}

std::optional<Vec2> GroundPainterInputControl::LastPoint_() const {
    if (Is_(Gesture::MovePoint)) {
        const ShapeSpec& shape = ShapeOf(pointEdit_, editShape_);
        const std::vector<Vec2>& points = shape.points;
        if (points.size() < 2) {
            return std::nullopt;
        }
        if (editPoint_ > 0) {
            return points[editPoint_ - 1];
        }
        return PointsClosed(shape) ? points.back() : points[1];
    }
    if (!anchors_.empty() && !Is_(Gesture::Stroke)) {
        return anchors_.back();
    }
    return std::nullopt;
}

void GroundPainterInputControl::SnapCursor_(const int32_t screenX, const int32_t screenZ) {
    snap_.reset();
    const bool snapsPoints = SnapsPoints_();
    const std::optional<Vec2> last = snapsPoints ? LastPoint_() : std::nullopt;
    const Vec2 raw = cursor_;

    if (snapsPoints && last && KeyHeld(VK_SHIFT)) {
        cursor_ = SnapToAngle(*last, raw, kAngleStep);
        snap_ = SnapResult{cursor_, SnapKind::Angle, {}, {}};
        return;
    }

    if (snapsPoints && settings_.snapToPaint && document_) {
        UpdateSnapIndex_();
        std::optional<SnapResult> found = snapIndex_.Snap(raw, snapReach_);
        // The points of the shape being drawn count too, to close it or line up with it.
        for (size_t i = 0; !found && i + 1 < anchors_.size(); ++i) {
            if (std::hypot(anchors_[i].x - raw.x, anchors_[i].z - raw.z) <= snapReach_) {
                found = SnapResult{anchors_[i], SnapKind::Point, {}, {}};
            }
        }
        if (found) {
            if (found->kind == SnapKind::Edge || found->kind == SnapKind::Middle) {
                alignDirection_ = Vec2{found->edgeTo.x - found->edgeFrom.x, found->edgeTo.z - found->edgeFrom.z};
            }
            cursor_ = found->point;
            snap_ = found;
            return;
        }
        if (last && alignDirection_) {
            if (const auto aligned = SnapToDirection(*last, raw, *alignDirection_, kAlignTolerance)) {
                cursor_ = aligned->point;
                snap_ = aligned;
                return;
            }
        }
    }

    if (settings_.snapPointsToGrid && !Is_(Gesture::PenHandle) && (settings_.tool != Tool::Select || Is_(Gesture::MovePoint))) {
        // The other painters' grid: at least 1 m.
        const float step = std::max(settings_.gridStepMeters, 1.0f);
        cursor_ = {std::round(cursor_.x / step) * step, std::round(cursor_.z / step) * step};
    }
}

void GroundPainterInputControl::UpdateSnapIndex_() {
    const uint32_t leftOut = Is_(Gesture::MovePoint) ? pointEdit_.id : 0;
    if (snapRevision_ == document_->Revision() && snapLeftOut_ == leftOut) {
        return;
    }
    snapRevision_ = document_->Revision();
    snapLeftOut_ = leftOut;
    snapIndex_.Clear();
    for (const PaintObject& object : document_->Objects()) {
        if (object.hidden || object.id == leftOut) {
            continue;
        }
        const ShapeSpec& shape = object.shape;
        if (shape.kind == ShapeKind::Marking && shape.marking.kind == MarkingKind::Text) {
            continue;  // letters are all corners: nothing to line up with
        }
        snapIndex_.AddArea(document_->AreaOf(object.id));
        if (shape.kind == ShapeKind::Line || shape.kind == ShapeKind::Area ||
            (shape.kind == ShapeKind::Marking && !PointsFor(shape.marking.kind).closed)) {
            // The clicked points, and a line's middle, which its outline does not show.
            for (const Vec2& point : shape.points) {
                snapIndex_.AddPoint(point);
            }
        }
        if (shape.kind == ShapeKind::Line) {
            const std::vector<Vec2> path = ShapePath(shape, false);
            for (size_t i = 0; i + 1 < path.size(); ++i) {
                snapIndex_.AddSegment(path[i], path[i + 1], shape.pathStyle == PathStyle::Straight && shape.handles.empty());
            }
        }
    }
    LOG_DEBUG("Ground: snap index of {} points and {} edges", snapIndex_.PointCount(), snapIndex_.SegmentCount());
}

bool GroundPainterInputControl::OnMouseDownL(const int32_t x, const int32_t z, const uint32_t modifiers) {
    if (!active_ || !IsOnTop() || !document_ || !UpdateCursor_(x, z)) {
        return false;
    }
    if (IsMiddle_()) {
        return true;
    }
    if (gesture_ != Gesture::None) {
        OnMouseUpL(x, z, modifiers);  // its release went elsewhere, such as to the panel
    }

    if (settings_.tool == Tool::Select) {
        const bool add = (modifiers & MOD_CONTROL) != 0 || KeyHeld(VK_CONTROL);
        if (KeyHeld(VK_SHIFT) && StartTextureDrag_()) {
            return true;
        }
        if (StartPointEdit_(add, KeyHeld(VK_MENU))) {
            RefreshPreview_();
            return true;
        }
        const auto hit = document_->HitTest(cursor_);
        if (!hit) {
            if (!add) {
                document_->ClearSelection();
            }
        }
        else if (!document_->IsSelected(*hit) || !add) {
            if (!document_->IsSelected(*hit)) {
                document_->Select(*hit, add);
            }
            gesture_ = Gesture::MoveSelection;
            dragStart_ = cursor_;
        }
        RefreshPreview_();
        return true;
    }

    // The eyedropper: Alt+click, or a click after "Copy look" in the panel. With the pen, Alt on the
    // last point is for pulling out a new handle on one side.
    const bool penOnLastPoint = UsesPen_() && !anchors_.empty() && Distance(cursor_, anchors_.back()) <= snapReach_;
    if (settings_.tool != Tool::Erase && !penOnLastPoint && (lookPickArmed_ || KeyHeld(VK_MENU))) {
        lookPickArmed_ = false;
        PickLook_();
        RefreshPreview_();
        return true;
    }

    if (UsesStroke_()) {
        gesture_ = Gesture::Stroke;
        stroke_ = {cursor_};
        RefreshPreview_();
        return true;
    }

    if (ClosesShape_() && anchors_.size() >= 3 && Distance(cursor_, anchors_.front()) < CloseDistance_()) {
        Commit_();
        return true;
    }
    // With the pen, a press on the last point takes up its handle again.
    if (anchors_.empty() || Distance(cursor_, anchors_.back()) > (UsesPen_() ? snapReach_ : 0.01f)) {
        anchors_.push_back(cursor_);
    }
    if (UsesPen_()) {
        anchorHandles_.resize(anchors_.size());
        anchorInHandles_.resize(anchors_.size());
        penHandleBefore_ = anchorHandles_.back();
        penInHandleBefore_ = anchorInHandles_.back();
        gesture_ = Gesture::PenHandle;
        penPressX_ = x;
        penPressZ_ = z;
    }
    if (const size_t most = MarkingMaxPoints_(); most > 0 && anchors_.size() >= most) {
        Commit_();
        return true;
    }
    RefreshPreview_();
    return true;
}

bool GroundPainterInputControl::OnMouseUpL(const int32_t x, const int32_t z, const uint32_t /*modifiers*/) {
    if (!active_ || !document_) {
        return false;
    }
    UpdateCursor_(x, z);
    switch (gesture_) {
    case Gesture::PenHandle:
        gesture_ = Gesture::None;
        RefreshPreview_();
        return true;
    case Gesture::Stroke:
        FinishStroke_();
        return true;
    case Gesture::MovePoint:
    case Gesture::MoveHandle:
        FinishPointDrag_();
        return true;
    case Gesture::MoveTexture:
        FinishTextureDrag_();
        return true;
    case Gesture::MoveSelection:
        FinishDrag_();
        return true;
    default:
        return false;
    }
}

bool GroundPainterInputControl::OnMouseMove(const int32_t x, const int32_t z, const uint32_t /*modifiers*/) {
    if (!active_ || !IsOnTop()) {
        return false;
    }
    if (!UpdateCursor_(x, z)) {
        return true;
    }

    // The game can send several moves a frame: they only update where things are, and
    // ProcessPendingActions shows the result once per tick.
    moved_ = true;
    if (lookPickArmed_ || KeyHeld(VK_MENU)) {
        if (std::vector<uint32_t> under = document_ ? document_->HitTestAll(cursor_) : std::vector<uint32_t>{};
            under != lookPickUnder_) {
            lookPickUnder_ = std::move(under);
            lookPickCycle_ = 0;
        }
    }
    if (IsMiddle_()) {
        const float dx = static_cast<float>(x - middleStartX_);
        if (Is_(Gesture::MiddleBrush)) {
            SetBrushRadius_(middleStartRadius_ * std::exp(dx * kBrushPerPixel));
        }
        else {
            middleAmount_ = Is_(Gesture::MiddleScale) ? std::exp(dx * kScalePerPixel) : dx * kTurnPerPixel;
        }
        return true;
    }

    if (Is_(Gesture::Stroke)) {
        const float spacing = settings_.tool == Tool::Brush || settings_.tool == Tool::Erase
                                  ? std::max(0.25f, settings_.brushRadius * 0.25f)
                                  : kFreehandSpacing;
        if (stroke_.empty() || Distance(stroke_.back(), cursor_) >= spacing) {
            stroke_.push_back(cursor_);
        }
    }
    else if (Is_(Gesture::PenHandle) && !anchors_.empty()) {
        const bool pulled = std::hypot(static_cast<float>(x - penPressX_), static_cast<float>(z - penPressZ_)) >= kPenDragPixels;
        const Vec2 anchor = anchors_.back();
        const Vec2 aim = KeyHeld(VK_SHIFT) ? SnapToAngle(anchor, cursor_, kAngleStep) : cursor_;
        if (!pulled) {
            anchorHandles_.back() = penHandleBefore_;
            anchorInHandles_.back() = penInHandleBefore_;
        }
        else {
            anchorHandles_.back() = {aim.x - anchor.x, aim.z - anchor.z};
            if (!KeyHeld(VK_MENU)) {  // Alt leaves the side the curve arrives from as it is
                anchorInHandles_.back() = {anchor.x - aim.x, anchor.z - aim.z};
            }
        }
    }
    else if (Is_(Gesture::MovePoint)) {
        ShapeOf(pointEdit_, editShape_).points[editPoint_] = cursor_;
    }
    else if (Is_(Gesture::MoveHandle)) {
        ShapeSpec& shape = ShapeOf(pointEdit_, editShape_);
        const Vec2 anchor = shape.points[editPoint_];
        const Vec2 aim = KeyHeld(VK_SHIFT) ? SnapToAngle(anchor, cursor_, kAngleStep) : cursor_;
        Vec2 pulled{aim.x - anchor.x, aim.z - anchor.z};
        if (std::hypot(pulled.x, pulled.z) < snapReach_ * 0.5f) {
            pulled = {};  // back on the point: no handle
        }
        Vec2 handle = shape.handles[editPoint_];
        Vec2 inHandle = InHandleOf(shape, editPoint_);
        if (handleSplit_) {
            (handleSign_ > 0.0f ? handle : inHandle) = pulled;
        }
        else {
            handle = {pulled.x * handleSign_, pulled.z * handleSign_};
            inHandle = {-handle.x, -handle.z};
        }
        SetPointHandles(shape, editPoint_, handle, inHandle);
    }
    return true;
}

void GroundPainterInputControl::ShowMove_() {
    moved_ = false;
    if (!document_) {
        return;
    }
    if (Is_(Gesture::MiddleTurn)) {
        TransformSelection_(middleAmount_, 1.0f, true);
    }
    else if (Is_(Gesture::MiddleScale)) {
        TransformSelection_(0.0f, middleAmount_, true);
    }
    else if (EditingPoints_()) {
        document_->ShowChanged(pointEdit_);
    }
    else if (Is_(Gesture::MoveTexture)) {
        // A new style for each: no new cut.
        for (const PaintObject& object : TextureDragged_()) {
            document_->ShowChanged(object);
        }
    }
    else if (Is_(Gesture::MoveSelection)) {
        document_->ShowSelectionMoved({cursor_.x - dragStart_.x, cursor_.z - dragStart_.z});
    }
    RefreshPreview_();
}

bool GroundPainterInputControl::OnKeyDown(const int32_t vkCode, const uint32_t modifiers) {
    if (!active_ || !IsOnTop() || !document_) {
        return false;
    }
    const bool ctrl = (modifiers & MOD_CONTROL) != 0 || KeyHeld(VK_CONTROL);

    switch (vkCode) {
    case VK_ESCAPE:
        if (IsDrawing()) {
            CancelShape_();
        }
        else if (settings_.tool == Tool::Select && Picked_()) {
            pickedObject_ = 0;
            RefreshPreview_();
        }
        else if (settings_.tool == Tool::Select && returnTool_) {
            // Back to the tool the selected shape was drawn with.
            settings_.tool = *std::exchange(returnTool_, std::nullopt);
            document_->ClearSelection();
            RefreshPreview_();
        }
        else {
            cancelPending_ = true;
        }
        return true;
    case VK_RETURN:
        Commit_();
        return true;
    case VK_BACK:
        if (ctrl) {
            Undo_();
        }
        else if (!anchors_.empty()) {
            anchors_.pop_back();
            anchorHandles_.resize(anchors_.size());
            anchorInHandles_.resize(anchors_.size());
            RefreshPreview_();
        }
        return true;
    case VK_DELETE:
        if (settings_.tool == Tool::Select && Picked_()) {
            RemovePickedPoint_();
        }
        else if (settings_.tool == Tool::Select && !document_->Selection().empty()) {
            const std::vector<uint32_t> selected = document_->Selection();
            document_->Remove(selected);
            RefreshPreview_();
        }
        return true;
    case VK_OEM_4:
    case VK_OEM_6:
        if (!ctrl && UsesBrush_()) {
            SetBrushRadius_(vkCode == VK_OEM_6 ? settings_.brushRadius * kBrushStep : settings_.brushRadius / kBrushStep);
            RefreshPreview_();
            return true;
        }
        break;
    case VK_OEM_COMMA:
    case VK_OEM_PERIOD:
        if (settings_.tool == Tool::Select) {
            const float step = KeyHeld(VK_SHIFT) ? kFineTurnStep : kTurnStep;
            TransformSelection_(vkCode == VK_OEM_PERIOD ? step : -step, 1.0f, false);
            return true;
        }
        break;
    case VK_OEM_MINUS:
    case VK_OEM_PLUS:
        if (settings_.tool == Tool::Select) {
            TransformSelection_(0.0f, vkCode == VK_OEM_PLUS ? kScaleStep : 1.0f / kScaleStep, false);
            return true;
        }
        break;
    default:
        break;
    }

    switch (vkCode) {
    case 'G':
        if (ctrl) {
            break;
        }
        // As in the other painters: G shows the grid, Shift+S snaps to it, Ctrl+[ and Ctrl+]
        // change its step.
        settings_.showGrid = !settings_.showGrid;
        if (onGridChanged_) {
            onGridChanged_();
        }
        RefreshPreview_();
        return true;
    case 'S':
        if (!KeyHeld(VK_SHIFT)) {
            return false;
        }
        settings_.snapPointsToGrid = !settings_.snapPointsToGrid;
        if (onGridChanged_) {
            onGridChanged_();
        }
        return true;
    case 'M':
        if (ctrl) {
            return false;
        }
        settings_.snapToPaint = !settings_.snapToPaint;
        snap_.reset();
        RefreshPreview_();
        return true;
    case VK_OEM_4:
    case VK_OEM_6:
        if (ctrl) {
            constexpr float kSteps[] = {1.0f, 2.0f, 4.0f, 8.0f, 16.0f};
            const auto nearest = std::ranges::min_element(kSteps, {}, [&](const float step) {
                return std::abs(step - settings_.gridStepMeters);
            });
            const std::ptrdiff_t index = std::clamp<std::ptrdiff_t>(
                (nearest - std::begin(kSteps)) + (vkCode == VK_OEM_6 ? 1 : -1), 0, std::size(kSteps) - 1);
            settings_.gridStepMeters = kSteps[index];
            if (onGridChanged_) {
                onGridChanged_();
            }
            RefreshPreview_();
            return true;
        }
        return false;
    default:
        break;
    }

    switch (vkCode) {
    case 'D':
        if (ctrl && settings_.tool == Tool::Select) {
            const std::vector<uint32_t> selected = document_->Selection();
            document_->Duplicate(selected, kDuplicateOffset);
            RefreshPreview_();
            return true;
        }
        return false;
    case 'G':
        if (ctrl && settings_.tool == Tool::Select) {
            const std::vector<uint32_t> selected = document_->Selection();
            if (KeyHeld(VK_SHIFT)) {
                document_->Ungroup(selected);
            }
            else {
                document_->Group(selected);
            }
            return true;
        }
        return false;
    case 'Z':
        if (ctrl) {
            Undo_();
            return true;
        }
        return false;
    case 'Y':
        if (ctrl) {
            Redo_();
            return true;
        }
        return false;
    default:
        return false;
    }
}

bool GroundPainterInputControl::OnMouseDownM(const int32_t x, const int32_t z, const uint32_t modifiers) {
    if (!active_ || !document_) {
        return false;
    }
    if (IsMiddle_()) {
        OnMouseUpM(x, z, modifiers);  // its release went elsewhere
    }
    if (gesture_ != Gesture::None) {
        return true;
    }
    UpdateCursor_(x, z);
    middleStartX_ = x;
    if (UsesBrush_()) {
        // Drag sideways to size the brush ([ and ]).
        gesture_ = Gesture::MiddleBrush;
        middleStartRadius_ = settings_.brushRadius;
        return true;
    }
    if (settings_.tool == Tool::Select && !document_->Selection().empty()) {
        // Drag sideways to turn the selection (, and .), with Shift to scale it (- and =).
        const Bounds bounds = document_->SelectionBounds();
        middlePivot_ = {(bounds.min.x + bounds.max.x) * 0.5f, (bounds.min.z + bounds.max.z) * 0.5f};
        gesture_ = KeyHeld(VK_SHIFT) ? Gesture::MiddleScale : Gesture::MiddleTurn;
        middleAmount_ = Is_(Gesture::MiddleScale) ? 1.0f : 0.0f;
        return true;
    }
    if (!anchors_.empty()) {
        // A click finishes the shape being drawn (Enter).
        Commit_();
        return true;
    }
    return false;
}

bool GroundPainterInputControl::OnMouseUpM(const int32_t /*x*/, const int32_t /*z*/, const uint32_t /*modifiers*/) {
    if (!IsMiddle_()) {
        return false;
    }
    const Gesture drag = std::exchange(gesture_, Gesture::None);
    if (drag == Gesture::MiddleTurn && middleAmount_ != 0.0f) {
        TransformSelection_(middleAmount_, 1.0f, false);
    }
    else if (drag == Gesture::MiddleScale && middleAmount_ != 1.0f) {
        TransformSelection_(0.0f, middleAmount_, false);
    }
    return true;
}

bool GroundPainterInputControl::OnMouseDownX(const int32_t /*x*/, const int32_t /*z*/, const uint32_t /*modifiers*/,
                                             const uint32_t button) {
    if (!active_ || !document_) {
        return false;
    }
    if (button == XBUTTON1) {
        // Back: the last point while drawing (Backspace), otherwise undo (Ctrl+Z).
        if (!anchors_.empty()) {
            anchors_.pop_back();
            anchorHandles_.resize(anchors_.size());
            anchorInHandles_.resize(anchors_.size());
            RefreshPreview_();
        }
        else {
            Undo_();
        }
        return true;
    }
    if (button == XBUTTON2) {
        Redo_();  // Ctrl+Y
        return true;
    }
    return false;
}

bool GroundPainterInputControl::UsesBrush_() const {
    return settings_.tool == Tool::Brush || (settings_.tool == Tool::Erase && settings_.eraseWithBrush);
}

void GroundPainterInputControl::Undo_() {
    document_->Undo();
    RefreshPreview_();
}

void GroundPainterInputControl::Redo_() {
    document_->Redo();
    RefreshPreview_();
}

void GroundPainterInputControl::SetBrushRadius_(const float radius) {
    settings_.brushRadius = std::clamp(radius, 0.25f, 64.0f);
}

void GroundPainterInputControl::TransformSelection_(const float turnDegrees, const float scale, const bool preview) {
    if (document_->Selection().empty()) {
        return;
    }
    if (!preview) {
        // Keys turn about the middle as it is now; a drag keeps the middle it started with.
        if (!IsMiddle_()) {
            const Bounds bounds = document_->SelectionBounds();
            middlePivot_ = {(bounds.min.x + bounds.max.x) * 0.5f, (bounds.min.z + bounds.max.z) * 0.5f};
        }
    }
    const Placement placement{
        .pivot = middlePivot_,
        .rotationRadians = turnDegrees * 3.14159265f / 180.0f,
        .scale = std::max(0.1f, scale),
    };
    if (preview) {
        document_->ShowSelectionTransformed(placement);
        return;
    }
    const std::vector<uint32_t> selected = document_->Selection();
    document_->Transform(selected, placement);
    // Locked ones were shown turned too; show them as they are.
    document_->ShowSelectionMoved({});
    RefreshPreview_();
}

PaintObject GroundPainterInputControl::ObjectFrom_(const std::vector<Vec2>& points, const bool curved) const {
    PaintObject object;
    object.look = settings_.ActiveLook();
    ShapeSpec& shape = object.shape;
    shape.points = points;
    shape.pathStyle = curved ? PathStyle::Curved : PathStyle::Straight;
    if (UsesPen_()) {
        shape.handles = anchorHandles_;
        shape.inHandles = anchorInHandles_;
    }
    shape.wear = settings_.tool == Tool::Erase ? 0.0f : settings_.wear;
    shape.roughness = settings_.tool == Tool::Erase || settings_.tool == Tool::Marking ? 0.0f : settings_.roughness;
    switch (settings_.tool) {
    case Tool::Line:
    case Tool::Path:
        shape.kind = ShapeKind::Line;
        shape.width = settings_.ActiveLineWidth();
        shape.ends = settings_.lineEnds;
        shape.dashLength = settings_.dashLength;
        shape.gapLength = settings_.gapLength;
        break;
    case Tool::Brush:
        shape.kind = ShapeKind::Brush;
        shape.brushRadius = settings_.brushRadius;
        break;
    case Tool::Marking:
        shape.kind = ShapeKind::Marking;
        shape.marking = settings_.marking;
        break;
    default:
        shape.kind = ShapeKind::Area;
        shape.cornerRadius = settings_.tool == Tool::Area ? settings_.cornerRadius : 0.0f;
        break;
    }
    return object;
}

std::vector<Vec2> GroundPainterInputControl::PointsWithCursor_() const {
    std::vector<Vec2> points = anchors_;
    if (cursorValid_ && !Is_(Gesture::PenHandle) && !points.empty() && Distance(points.back(), cursor_) > 0.01f) {
        points.push_back(cursor_);
    }
    return points;
}

void GroundPainterInputControl::Commit_() {
    if (!document_ || anchors_.empty()) {
        return;
    }
    const PaintObject object = ObjectFrom_(anchors_, DrawsCurve_());
    if (settings_.tool == Tool::Erase) {
        document_->Erase(object.shape);
    }
    else if (anchors_.size() >= MinimumPoints(object)) {
        const uint32_t id = document_->Add(object);
        LogAdded_(object, id);
        SelectPainted_(id);
    }
    else {
        LOG_INFO("Ground: not painted, the shape has {} of the {} points it needs", anchors_.size(), MinimumPoints(object));
    }
    CancelShape_();
}

void GroundPainterInputControl::SelectPainted_(const uint32_t id) {
    if (id == 0 || settings_.keepDrawing || (settings_.tool != Tool::Area && !DrawsLine(settings_.tool))) {
        return;
    }
    returnTool_ = settings_.tool;
    settings_.tool = Tool::Select;
    document_->Select(id, false);
}

void GroundPainterInputControl::LogAdded_(const PaintObject& object, const uint32_t id) const {
    const char* const kind = object.shape.kind == ShapeKind::Marking ? MarkingKindName(object.shape.marking.kind)
                                                                     : ShapeKindName(object.shape.kind);
    if (id == 0) {
        LOG_WARN("Ground: the {} covers nothing, so it was not painted ({} points)", kind, object.shape.points.size());
        return;
    }
    const PaintObject* added = document_->Find(id);
    LOG_INFO("Ground: painted {} {} ({} rings, terrain paint {})", kind, id, document_->AreaOf(id).size(),
             added ? added->paintId : 0u);
}

void GroundPainterInputControl::FinishStroke_() {
    gesture_ = Gesture::None;
    if (stroke_.empty() || !document_) {
        CancelShape_();
        return;
    }
    if (stroke_.back() != cursor_ && cursorValid_) {
        stroke_.push_back(cursor_);
    }

    if (settings_.tool == Tool::Erase) {
        // Fewer points to drag later; a tenth of the brush is not seen.
        document_->Erase(ShapeSpec{.kind = ShapeKind::Brush,
                                   .points = SimplifyPath(stroke_, settings_.brushRadius * 0.1f, false),
                                   .brushRadius = settings_.brushRadius});
    }
    else if (settings_.tool == Tool::Brush) {
        const PaintObject object = ObjectFrom_(stroke_, false);
        if (!JoinStroke_(object)) {
            LogAdded_(object, document_->Add(object));
        }
    }
    else {
        // Freehand area or line: a smooth curve through the simplified stroke.
        anchors_ = SimplifyPath(stroke_, kFreehandTolerance, settings_.tool == Tool::Area);
        stroke_.clear();
        Commit_();
        return;
    }
    CancelShape_();
}

bool GroundPainterInputControl::JoinStroke_(const PaintObject& stroke) {
    if (!settings_.joinBrushStrokes) {
        return false;
    }
    const Area area = BuildArea(stroke);
    const Bounds bounds = BoundsOf(area);
    const auto& objects = document_->Objects();
    // The topmost brush paint painted the same way that the stroke touches.
    for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
        const PaintObject& target = *it;
        if (target.shape.kind != ShapeKind::Brush || target.locked || target.hidden || !(target.look == stroke.look) ||
            target.shape.wear != stroke.shape.wear || target.shape.roughness != stroke.shape.roughness ||
            !Overlaps(bounds, document_->BoundsOf(target.id)) || IsEmpty(Intersect(area, document_->AreaOf(target.id)))) {
            continue;
        }
        // Everything painted so far, without its rough edge (that is drawn once, round the
        // whole), becomes the joined part; erased holes in it stay, the new stroke paints over them.
        PaintObject smooth = target;
        smooth.shape.roughness = 0.0f;
        PaintObject joined = target;
        joined.shape.outline = BuildArea(smooth);
        joined.erased.clear();
        joined.erasers.clear();
        joined.shape.points = stroke.shape.points;
        joined.shape.brushRadius = stroke.shape.brushRadius;
        document_->Update({joined});
        LOG_INFO("Ground: joined a brush stroke to {}", target.id);
        return true;
    }
    return false;
}

std::vector<PaintObject> GroundPainterInputControl::TextureDragged_() const {
    std::vector<PaintObject> moved;
    const Vec2 offset{cursor_.x - dragStart_.x, cursor_.z - dragStart_.z};
    for (const uint32_t id : document_->Selection()) {
        const PaintObject* const object = document_->Find(id);
        if (!object || object->locked || object->look.textures.empty() || FollowsLine(*object)) {
            continue;
        }
        moved.push_back(*object);
        moved.back().look.textureOffset[0] += offset.x;
        moved.back().look.textureOffset[1] += offset.z;
    }
    return moved;
}

bool GroundPainterInputControl::StartTextureDrag_() {
    const auto hit = document_->HitTest(cursor_);
    if (!hit) {
        return false;
    }
    if (!document_->IsSelected(*hit)) {
        document_->Select(*hit, false);
    }
    dragStart_ = cursor_;
    if (TextureDragged_().empty()) {
        return false;  // nothing with a flat texture: an ordinary click
    }
    gesture_ = Gesture::MoveTexture;
    RefreshPreview_();
    return true;
}

void GroundPainterInputControl::FinishTextureDrag_() {
    gesture_ = Gesture::None;
    if (std::hypot(cursor_.x - dragStart_.x, cursor_.z - dragStart_.z) >= 0.01f) {
        document_->Update(TextureDragged_());
    }
    document_->ShowSelectionMoved({});
    RefreshPreview_();
}

void GroundPainterInputControl::FinishDrag_() {
    gesture_ = Gesture::None;
    const Vec2 offset{cursor_.x - dragStart_.x, cursor_.z - dragStart_.z};
    if (std::hypot(offset.x, offset.z) < 0.01f) {
        document_->ShowSelectionMoved({});
        RefreshPreview_();
        return;
    }

    const std::vector<uint32_t> selected = document_->Selection();
    document_->Transform(selected, {.offset = offset});
    // Locked objects were shown moved too; put them back.
    document_->ShowSelectionMoved({});
    RefreshPreview_();
}

const PaintObject* GroundPainterInputControl::PointEditTarget_() const {
    if (!document_ || document_->Selection().size() != 1) {
        return nullptr;
    }
    const PaintObject* const object = document_->Find(document_->Selection().front());
    return object && !object->locked && !object->hidden && !EditableShapes(*object).empty() ? object : nullptr;
}

bool GroundPainterInputControl::StartPointEdit_(const bool insert, const bool remove) {
    const PaintObject* const object = PointEditTarget_();
    if (!object) {
        return false;
    }
    std::optional<std::pair<int, size_t>> nearest = PointNear_(*object);
    const auto at = [&](const std::pair<int, size_t> point) { return ShapeOf(*object, point.first).points[point.second]; };

    pointEdit_ = *object;
    if (!insert) {
        // The end of a curve handle, when it is nearer than any point.
        float best = nearest ? Distance(at(*nearest), cursor_) : PickDistance_();
        std::optional<std::pair<int, size_t>> tip;
        for (const int s : EditableShapes(*object)) {
            const ShapeSpec& shape = ShapeOf(*object, s);
            for (size_t i = 0; i < std::min(shape.handles.size(), shape.points.size()); ++i) {
                for (const float sign : {1.0f, -1.0f}) {
                    const Vec2 offset = sign > 0.0f ? shape.handles[i] : InHandleOf(shape, i);
                    const float distance = Distance({shape.points[i].x + offset.x, shape.points[i].z + offset.z}, cursor_);
                    if (offset != Vec2{} && distance < best) {
                        best = distance;
                        tip = std::pair{s, i};
                        handleSign_ = sign;
                    }
                }
            }
        }
        if (tip) {
            const ShapeSpec& shape = ShapeOf(*object, tip->first);
            const Vec2 handle = shape.handles[tip->second];
            // Alt splits the two ends. Ends that are already apart stay apart.
            handleSplit_ = remove || !(InHandleOf(shape, tip->second) == Vec2{-handle.x, -handle.z});
            BeginPointGesture_(Gesture::MoveHandle, *tip);
            return true;
        }
    }
    if (remove) {
        // Alt on a point, as Illustrator's Convert Anchor Point: a drag pulls out new handles, a
        // click leaves a corner.
        if (!nearest) {
            return false;
        }
        ShapeSpec& shape = ShapeOf(pointEdit_, nearest->first);
        if (shape.kind != ShapeKind::Area && shape.kind != ShapeKind::Line) {
            return true;
        }
        if (shape.pathStyle != PathStyle::Straight) {
            // The same curve, with a handle on every point.
            const bool closed = PointsClosed(shape);
            shape.pathStyle = PathStyle::Straight;
            for (size_t i = 0; i < shape.points.size(); ++i) {
                const Vec2 smooth = SmoothHandle(shape.points, i, closed);
                SetPointHandles(shape, i, smooth, {-smooth.x, -smooth.z});
            }
        }
        SetPointHandles(shape, nearest->second, {}, {});
        handleSign_ = 1.0f;
        handleSplit_ = false;
        BeginPointGesture_(Gesture::MoveHandle, *nearest);
        return true;
    }
    if (!nearest && (insert || !WideLine_(*object))) {
        if (const auto spot = InsertSpot_(*object)) {
            nearest = std::pair{spot->shape, spot->index};
            InsertShapePoint(ShapeOf(pointEdit_, spot->shape), spot->index, spot->at);
        }
    }
    if (!nearest) {
        return false;
    }
    BeginPointGesture_(Gesture::MovePoint, *nearest);
    return true;
}

void GroundPainterInputControl::BeginPointGesture_(const Gesture gesture, const std::pair<int, size_t> point) {
    gesture_ = gesture;
    editShape_ = pickedShape_ = point.first;
    editPoint_ = pickedIndex_ = point.second;
    pickedObject_ = pointEdit_.id;
    document_->ShowChanged(pointEdit_);
}

bool GroundPainterInputControl::RemovePickedPoint_() {
    const auto picked = Picked_();
    if (!picked) {
        return false;
    }
    PaintObject changed = *PointEditTarget_();
    ShapeSpec& shape = ShapeOf(changed, picked->first);
    if (shape.points.size() > MinimumPoints(shape)) {
        EraseShapePoint(shape, picked->second);
    }
    else if (picked->first >= 0) {
        changed.erasers.erase(changed.erasers.begin() + picked->first);
    }
    else {
        return false;
    }
    pickedObject_ = 0;
    document_->Update({changed});
    RefreshPreview_();
    return true;
}

std::optional<std::pair<int, size_t>> GroundPainterInputControl::PointNear_(const PaintObject& object) const {
    std::optional<std::pair<int, size_t>> nearest;
    float best = PickDistance_();
    for (const int s : EditableShapes(object)) {
        const std::vector<Vec2>& points = ShapeOf(object, s).points;
        for (size_t i = 0; i < points.size(); ++i) {
            if (const float distance = Distance(points[i], cursor_); distance < best) {
                best = distance;
                nearest = std::pair{s, i};
            }
        }
    }
    return nearest;
}

std::optional<GroundPainterInputControl::InsertSpot> GroundPainterInputControl::InsertSpot_(const PaintObject& object) const {
    std::optional<InsertSpot> best;
    float bestDistance = PickDistance_();
    for (const int s : EditableShapes(object)) {
        const ShapeSpec& shape = ShapeOf(object, s);
        const std::vector<Vec2>& points = shape.points;
        const size_t most = PointsFor(shape.marking.kind).maxPoints;
        if (points.size() < 2 || (shape.kind == ShapeKind::Marking && most > 0 && points.size() >= most)) {
            continue;
        }
        const bool closed = PointsClosed(shape);

        // Along the drawn path, so a click on a curve lands on it. Rounded corners miss their points: then the straight sides.
        std::vector<Vec2> path;
        std::vector<size_t> pointAt;
        if (shape.kind == ShapeKind::Area || shape.kind == ShapeKind::Line) {
            path = ShapePath(shape, closed);
            size_t k = 0;
            for (const Vec2& point : points) {
                while (k < path.size() && Distance(path[k], point) > 0.05f) {
                    ++k;
                }
                if (k == path.size()) {
                    break;
                }
                pointAt.push_back(k);
            }
            if (pointAt.size() != points.size()) {
                path.clear();
            }
        }

        const auto consider = [&](const size_t side, const Vec2 a, const Vec2 b) {
            const Vec2 spot = ClosestOnSegment(cursor_, a, b);
            if (const float distance = Distance(cursor_, spot); distance < bestDistance) {
                bestDistance = distance;
                best = InsertSpot{s, side + 1, spot};
            }
        };
        if (!path.empty()) {
            const size_t edges = closed ? path.size() : path.size() - 1;
            size_t side = 0;
            for (size_t k = 0; k < edges; ++k) {
                while (side + 1 < pointAt.size() && pointAt[side + 1] <= k) {
                    ++side;
                }
                consider(side, path[k], path[(k + 1) % path.size()]);
            }
        }
        else {
            const size_t sides = closed ? points.size() : points.size() - 1;
            for (size_t i = 0; i < sides; ++i) {
                consider(i, points[i], points[(i + 1) % points.size()]);
            }
        }
    }
    return best;
}

void GroundPainterInputControl::FinishPointDrag_() {
    gesture_ = Gesture::None;
    document_->Update({pointEdit_});
    // Shows the object as the document has it, also when the change painted nothing.
    document_->ShowSelectionMoved({});
    RefreshPreview_();
}

void GroundPainterInputControl::CancelShape_() {
    anchors_.clear();
    anchorHandles_.clear();
    anchorInHandles_.clear();
    if (Is_(Gesture::PenHandle) || Is_(Gesture::Stroke)) {
        gesture_ = Gesture::None;
    }
    stroke_.clear();
    alignDirection_.reset();
    if (document_) {
        document_->ShowPreview(nullptr);
    }
    RefreshPreview_();
}

cISTETerrain* GroundPainterInputControl::Terrain_() const {
    return city_ ? city_->GetTerrain() : nullptr;
}

void GroundPainterInputControl::RefreshPreview_() {
    // Up to date now, whatever asked for it.
    moved_ = false;
    if (document_) {
        shownSelection_ = document_->Selection();
    }
    std::vector<PaintOverlay::GroundGuide> guides;
    std::vector<Vec2> markers;
    std::optional<std::pair<Vec2, bool>> hoverPoint;
    std::optional<Vec2> addSpot;
    float ring = 0.0f;
    uint32_t ringColor = kCursorPathColor;
    // Curve handles: a line from the point to each end, and a marker on the end.
    const auto showHandles = [&](const std::vector<Vec2>& points, const std::vector<Vec2>& handles,
                                 const std::vector<Vec2>& inHandles) {
        for (size_t i = 0; i < std::min(points.size(), handles.size()); ++i) {
            const Vec2 inHandle = i < inHandles.size() ? inHandles[i] : Vec2{-handles[i].x, -handles[i].z};
            for (const Vec2 offset : {handles[i], inHandle}) {
                if (offset != Vec2{}) {
                    const Vec2 tip{points[i].x + offset.x, points[i].z + offset.z};
                    guides.push_back({{points[i], tip}, false, kSnapGuideColor});
                    markers.push_back(tip);
                }
            }
        }
    };

    if (document_) {
        if (settings_.tool == Tool::Select) {
            for (const uint32_t selected : document_->Selection()) {
                const Area area = EditingPoints_() && selected == pointEdit_.id ? BuildArea(pointEdit_)
                                                                                         : document_->AreaOf(selected);
                for (const Ring& outline : area) {
                    guides.push_back({outline, true, kSelectionColor});
                }
            }
            // The points of the shape and of its holes; the holes' outlines in the eraser colour.
            const auto showPoints = [&](const PaintObject& object) {
                for (const int s : EditableShapes(object)) {
                    const ShapeSpec& shape = ShapeOf(object, s);
                    markers.insert(markers.end(), shape.points.begin(), shape.points.end());
                    showHandles(shape.points, shape.handles, shape.inHandles);
                    if (s >= 0) {
                        const bool closed = PointsClosed(shape);
                        guides.push_back({closed ? ShapePath(shape, true) : shape.points, closed, kEraserColor});
                    }
                }
            };
            if (EditingPoints_()) {
                showPoints(pointEdit_);
            }
            else if (const PaintObject* const target = PointEditTarget_()) {
                showPoints(*target);
                if (const auto picked = Picked_()) {
                    // A diamond round the point that the panel's corner radius and Delete are for.
                    const Vec2 p = ShapeOf(*target, picked->first).points[picked->second];
                    const float kSize = std::max(0.3f, snapReach_ * 0.5f);
                    guides.push_back({{{p.x - kSize, p.z}, {p.x, p.z - kSize}, {p.x + kSize, p.z}, {p.x, p.z + kSize}}, true, kSnapPointColor});
                }
                if (cursorValid_ && !Is_(Gesture::MoveSelection) && !Is_(Gesture::MoveTexture)) {
                    if (const auto closest = PointNear_(*target)) {
                        hoverPoint = std::pair{ShapeOf(*target, closest->first).points[closest->second], KeyHeld(VK_MENU)};
                    }
                    else if (const auto spot = InsertSpot_(*target)) {
                        addSpot = spot->at;
                    }
                }
            }
            if (cursorValid_ && !Is_(Gesture::MoveSelection)) {
                if (const auto hover = document_->HitTest(cursor_); hover && !document_->IsSelected(*hover)) {
                    for (const Ring& outline : document_->AreaOf(*hover)) {
                        guides.push_back({outline, true, kHoverColor});
                    }
                }
            }
        }
        else if (Is_(Gesture::Stroke) || !anchors_.empty()) {
            const bool eraser = settings_.tool == Tool::Erase;
            const PaintObject drawing = Is_(Gesture::Stroke)
                                            ? ObjectFrom_(stroke_, false)
                                            : ObjectFrom_(PointsWithCursor_(), DrawsCurve_());
            if (Is_(Gesture::Stroke) && settings_.tool != Tool::Brush && !eraser) {
                // A freehand stroke shows as the path being drawn; it is smoothed on release.
                guides.push_back({stroke_, false, kPathColor});
                document_->ShowPreview(nullptr);
            }
            else if (eraser) {
                const Area area = Is_(Gesture::Stroke) ? BrushStroke(stroke_, settings_.brushRadius) : BuildArea(drawing);
                for (const Ring& outline : area) {
                    guides.push_back({outline, true, kEraserColor});
                }
            }
            else {
                document_->ShowPreview(&drawing);
            }
            if (!Is_(Gesture::Stroke)) {
                guides.push_back({DrawingPath_(), ClosesShape_(), eraser ? kEraserColor : kPathColor});
                markers = anchors_;
                showHandles(anchors_, anchorHandles_, anchorInHandles_);
            }
        }
        else {
            for (const uint32_t selected : document_->Selection()) {
                for (const Ring& outline : document_->AreaOf(selected)) {
                    guides.push_back({outline, true, kSelectionColor});
                }
            }
            if (settings_.tool != Tool::Erase && (lookPickArmed_ || KeyHeld(VK_MENU))) {
                if (const auto target = LookPickTarget_()) {
                    for (const Ring& outline : document_->AreaOf(*target)) {
                        guides.push_back({outline, true, kHoverColor});
                    }
                }
            }
        }
    }

    if (cursorValid_ && (settings_.tool == Tool::Brush || (settings_.tool == Tool::Erase && settings_.eraseWithBrush))) {
        ring = settings_.brushRadius;
        ringColor = settings_.tool == Tool::Erase ? kEraserColor : kCursorPathColor;
    }
    const auto circleAt = [](const Vec2 centre, const float size) {
        std::vector<Vec2> circle;
        constexpr int kMarkerPoints = 16;
        for (int i = 0; i < kMarkerPoints; ++i) {
            const float angle = 6.2831853f * static_cast<float>(i) / kMarkerPoints;
            circle.push_back({centre.x + size * std::cos(angle), centre.z + size * std::sin(angle)});
        }
        return circle;
    };
    const float cueSize = std::max(0.2f, snapReach_ * 0.6f);
    const auto crossAt = [&](const Vec2 centre, const float size, const bool diagonal, const uint32_t color) {
        const float a = diagonal ? size * 0.7071f : size;
        const float b = diagonal ? a : 0.0f;
        guides.push_back({{{centre.x - a, centre.z - b}, {centre.x + a, centre.z + b}}, false, color});
        guides.push_back({{{centre.x - b, centre.z + a}, {centre.x + b, centre.z - a}}, false, color});
    };
    if (hoverPoint) {
        const auto [point, converts] = *hoverPoint;
        guides.push_back({circleAt(point, cueSize), true, kSnapPointColor});
        if (converts) {
            // A handle bar: Alt+drag pulls out handles.
            guides.push_back({{{point.x - cueSize * 2.5f, point.z}, {point.x + cueSize * 2.5f, point.z}}, false, kSnapGuideColor});
        }
    }
    if (addSpot) {
        guides.push_back({circleAt(*addSpot, cueSize * 0.75f), true, kSnapPointColor});
        crossAt(*addSpot, cueSize * 0.5f, false, kSnapPointColor);
    }
    if (cursorValid_ && !Is_(Gesture::PenHandle) && UsesPen_() && ClosesShape_() && anchors_.size() >= 3 &&
        Distance(cursor_, anchors_.front()) < CloseDistance_()) {
        // A ring round the first point: a click here closes the shape.
        guides.push_back({circleAt(anchors_.front(), std::max(0.2f, snapReach_ * 0.75f)), true, kSnapPointColor});
    }
    if (cursorValid_ && snap_) {
        // What the point snapped to: a small ring on it, the edge it lies on, or the line it
        // was lined up along.
        const float size = std::max(0.1f, snapReach_ * 0.5f);
        const bool onPoint = snap_->kind == SnapKind::Point || snap_->kind == SnapKind::Middle;
        guides.push_back({circleAt(cursor_, size), true, onPoint ? kSnapPointColor : kSnapEdgeColor});
        if (snap_->kind == SnapKind::Edge || snap_->kind == SnapKind::Middle) {
            guides.push_back({{snap_->edgeFrom, snap_->edgeTo}, false, kSnapEdgeColor});
        }
        else if (const auto last = LastPoint_();
                 last && (snap_->kind == SnapKind::Parallel || snap_->kind == SnapKind::Square ||
                          snap_->kind == SnapKind::Angle)) {
            const float dx = cursor_.x - last->x;
            const float dz = cursor_.z - last->z;
            const float length = std::hypot(dx, dz);
            if (length > 0.01f) {
                const float reach = length + std::max(8.0f, length * 0.5f);
                guides.push_back({{*last, {last->x + dx / length * reach, last->z + dz / length * reach}}, false,
                                  kSnapGuideColor});
            }
        }
    }
    UpdateMeasure_();
    overlay_.BuildGroundPreview(guides, markers, kAnchorColor, cursorValid_, cursor_, ring, ringColor, settings_.showGrid,
                                settings_.gridStepMeters, Terrain_());
}

void GroundPainterInputControl::UpdateMeasure_() {
    measure_.clear();
    char text[192];
    const auto lengthOf = [](const std::vector<Vec2>& path, const bool closed) {
        double total = 0.0;
        for (size_t i = 1; i < path.size(); ++i) {
            total += std::hypot(path[i].x - path[i - 1].x, path[i].z - path[i - 1].z);
        }
        if (closed && path.size() > 2) {
            total += std::hypot(path.front().x - path.back().x, path.front().z - path.back().z);
        }
        return total;
    };
    const auto areaOf = [](const std::vector<Vec2>& ring) {
        double twice = 0.0;
        for (size_t i = 0; i < ring.size(); ++i) {
            const Vec2 a = ring[i];
            const Vec2 b = ring[(i + 1) % ring.size()];
            twice += static_cast<double>(a.x) * b.z - static_cast<double>(b.x) * a.z;
        }
        return std::abs(twice) * 0.5;
    };
    if (!cursorValid_ || Is_(Gesture::Stroke) || (!Is_(Gesture::MovePoint) && anchors_.empty())) {
        return;
    }
    const std::optional<Vec2> last = LastPoint_();
    if (!last) {
        return;
    }
    const float dx = cursor_.x - last->x;
    const float dz = cursor_.z - last->z;
    const float side = std::hypot(dx, dz);
    // The turn from the side before: 0 straight on, positive to the left.
    std::optional<float> turn;
    const std::vector<Vec2>& points = Is_(Gesture::MovePoint) ? ShapeOf(pointEdit_, editShape_).points : anchors_;
    if (!Is_(Gesture::MovePoint) && points.size() >= 2) {
        const Vec2 before = points[points.size() - 2];
        const float a0 = std::atan2(last->z - before.z, last->x - before.x);
        const float a1 = std::atan2(dz, dx);
        float delta = (a0 - a1) * kRadiansToDegrees;
        while (delta > 180.0f) delta -= 360.0f;
        while (delta < -180.0f) delta += 360.0f;
        turn = delta;
    }
    int used = turn ? std::snprintf(text, sizeof(text), "Side %.2f m, turning %.0f°%s.", side, std::abs(*turn),
                                    std::abs(*turn) < 0.5f ? "" : (*turn > 0.0f ? " left" : " right"))
                    : std::snprintf(text, sizeof(text), "%.2f m, at %.0f° to the grid.", side,
                                    std::atan2(-dz, dx) * kRadiansToDegrees);
    if (!Is_(Gesture::MovePoint) && settings_.tool != Tool::Select) {
        const bool closed = ClosesShape_();
        const std::vector<Vec2> path = DrawingPath_();
        if (closed && path.size() >= 3) {
            const double area = areaOf(path);
            used += std::snprintf(text + used, sizeof(text) - static_cast<size_t>(std::max(0, used)),
                                  " Round %.1f m, %.0f m² (%.2f tiles).", lengthOf(path, true), area, area / 256.0);
        }
        else if (path.size() >= 3) {
            used += std::snprintf(text + used, sizeof(text) - static_cast<size_t>(std::max(0, used)), " Length %.1f m.",
                                  lengthOf(path, false));
        }
    }
    measure_ = text;
    (void)used;
}

const std::string& GroundPainterInputControl::MeasureText() const {
    if (settings_.tool != Tool::Select || Is_(Gesture::MovePoint) || !document_) {
        return measure_;
    }
    static const std::string kNone;
    if (document_->Selection().size() != 1) {
        return kNone;
    }
    const uint32_t id = document_->Selection().front();
    if (id == selectionMeasureId_ && document_->Revision() == selectionMeasureRevision_) {
        return selectionMeasure_;
    }
    selectionMeasureId_ = id;
    selectionMeasureRevision_ = document_->Revision();
    selectionMeasure_.clear();
    const PaintObject* const object = document_->Find(id);
    if (!object) {
        return selectionMeasure_;
    }
    // Signed areas: holes run the other way round and take themselves off.
    double painted = 0.0;
    for (const Ring& ring : document_->AreaOf(id)) {
        for (size_t i = 0; i < ring.size(); ++i) {
            const Vec2 a = ring[i];
            const Vec2 b = ring[(i + 1) % ring.size()];
            painted += 0.5 * (static_cast<double>(a.x) * b.z - static_cast<double>(b.x) * a.z);
        }
    }
    painted = std::abs(painted);
    char text[128];
    if (object->shape.kind == ShapeKind::Line) {
        const std::vector<Vec2> path = ShapePath(object->shape, false);
        double length = 0.0;
        for (size_t i = 1; i < path.size(); ++i) {
            length += std::hypot(path[i].x - path[i - 1].x, path[i].z - path[i - 1].z);
        }
        std::snprintf(text, sizeof(text), "Length %.1f m, %.0f m² painted.", length, painted);
    }
    else {
        std::snprintf(text, sizeof(text), "%.0f m² painted (%.2f tiles).", painted, painted / 256.0);
    }
    selectionMeasure_ = text;
    return selectionMeasure_;
}

std::optional<uint32_t> GroundPainterInputControl::LookPickTarget_() const {
    if (!document_ || !cursorValid_) {
        return std::nullopt;
    }
    const std::vector<uint32_t> under = document_->HitTestAll(cursor_);
    if (under.empty()) {
        return std::nullopt;
    }
    const auto count = static_cast<int32_t>(under.size());
    return under[static_cast<size_t>(((lookPickCycle_ % count) + count) % count)];
}

bool GroundPainterInputControl::OnMouseWheel(const int32_t x, const int32_t z, const uint32_t modifiers,
                                             const int32_t wheelDelta) {
    // Alt+scroll changes which of the overlapping shapes the eyedropper copies. As in the texture
    // picker, at the cursor of the last mouse move: the wheel's own position can be off.
    if (active_ && IsOnTop() && document_ && wheelDelta != 0 && settings_.tool != Tool::Select &&
        settings_.tool != Tool::Erase && KeyHeld(VK_MENU) && cursorValid_ && document_->HitTestAll(cursor_).size() > 1) {
        lookPickCycle_ += wheelDelta > 0 ? 1 : -1;
        RefreshPreview_();
        return true;
    }
    return cSC4BaseViewInputControlEx::OnMouseWheel(x, z, modifiers, wheelDelta);
}

bool GroundPainterInputControl::PickLook_() {
    const auto hit = LookPickTarget_();
    const PaintObject* const object = hit ? document_->Find(*hit) : nullptr;
    if (!object) {
        return false;
    }
    settings_.ActiveLook() = object->look;
    settings_.wear = object->shape.wear;
    settings_.roughness = object->shape.kind == ShapeKind::Marking ? settings_.roughness : object->shape.roughness;
    ++lookPickCount_;
    return true;
}

std::string GroundPainterInputControl::ShortStatus() const {
    if (lookPickArmed_) {
        return "Click painted ground to copy its look.";
    }
    if (Is_(Gesture::PenHandle) || Is_(Gesture::MoveHandle)) {
        return "Drag to shape the curve. Release to keep it.";
    }
    if (Is_(Gesture::Stroke)) {
        return "Release the button to finish.";
    }
    const size_t points = anchors_.size();
    switch (settings_.tool) {
    case Tool::Area:
    case Tool::Line:
    case Tool::Path:
    case Tool::Erase: {
        const bool area = !DrawsLine(settings_.tool);
        if (settings_.tool == Tool::Erase && settings_.eraseWithBrush) {
            return "Drag over painted ground to remove it.";
        }
        if (settings_.drawStyle == DrawStyle::Freehand) {
            return area ? "Hold the left button and draw around the area." : "Hold the left button and draw the line.";
        }
        if (points == 0) {
            return area ? "Click in the city to place the first corner." : "Click in the city where the line starts.";
        }
        return area ? "Click to add corners. Click the first corner or press Enter to finish."
                    : "Click to continue the line. Press Enter to finish.";
    }
    case Tool::Brush:
        return "Hold the left button and drag to paint.";
    case Tool::Marking:
        return StatusText();
    case Tool::Select: {
        if (!document_ || document_->Selection().empty()) {
            return "Click painted ground to select it.";
        }
        if (Is_(Gesture::MoveTexture)) {
            return "Release the button to keep the texture here.";
        }
        if (const PaintObject* const target = PointEditTarget_()) {
            return Picked_() ? "Drag the point, or press Delete to remove it."
                             : WideLine_(*target) ? "Drag points to reshape. Ctrl+click the middle of the line to add a point."
                                                  : "Drag points to reshape. Click the outline to add a point.";
        }
        return "Drag to move the selection.";
    }
    }
    return {};
}

std::string GroundPainterInputControl::StatusText() const {
    if (lookPickArmed_) {
        return "Click painted ground to copy its look. If shapes overlap, hold Alt and scroll to change between them.";
    }
    if (Is_(Gesture::PenHandle) || Is_(Gesture::MoveHandle)) {
        return "Drag to shape the curve. Hold Shift to turn it in 15 degree steps. Hold Alt to bend one side only. Release the button to keep it.";
    }
    const size_t points = anchors_.size();
    const auto count = [&] { return std::to_string(points) + (points == 1 ? " point. " : " points. "); };
    const std::string finish = "Press Enter or middle-click to finish. Press Backspace to remove the last point.";

    switch (settings_.tool) {
    case Tool::Area:
    case Tool::Line:
    case Tool::Path:
    case Tool::Erase: {
        const bool area = !DrawsLine(settings_.tool);
        if (settings_.tool == Tool::Erase && settings_.eraseWithBrush) {
            return "Hold the left button and drag over painted ground to remove it. Press [ or ] to change the size.";
        }
        if (settings_.drawStyle == DrawStyle::Freehand) {
            return Is_(Gesture::Stroke) ? "Release the button to finish." : area ? "Hold the left button and draw around the area."
                                                              : "Hold the left button and draw the line.";
        }
        if (area && UsesPen_() && points >= 3 && cursorValid_ && Distance(cursor_, anchors_.front()) < CloseDistance_()) {
            return "Click to close the shape.";
        }
        const std::string pen = UsesPen_() ? "Click for a corner. Hold the button and drag for a curve. " : "";
        if (points == 0) {
            return pen + (area ? "Click in the city to place the first corner." : "Click in the city where the line starts.");
        }
        return count() + pen +
               (area ? "Click to add corners. To finish, click the first corner again or press Enter. "
                     : "Click to continue the line. ") +
               finish;
    }
    case Tool::Brush:
        return Is_(Gesture::Stroke) ? "Release the button to finish the stroke."
                             : "Hold the left button and drag to paint. Press [ or ], or middle-drag, to change the size.";
    case Tool::Marking:
        switch (settings_.marking.kind) {
        case MarkingKind::ParkingRow:
        case MarkingKind::CarPark:
            return points == 0 ? "Click where the front of the row starts. The bays go to the left of your click direction."
                               : count() + "Click along the front of the row. " + finish;
        case MarkingKind::Zebra:
            return points == 0 ? "Click one kerb, where the crossing starts." : "Now click the other kerb.";
        case MarkingKind::Arrow:
            return points == 0 ? "Click where the tail of the arrow goes." : "Now click where its tip goes.";
        case MarkingKind::DoubleLine:
            return points == 0 ? "Click where the lines start." : count() + "Click to continue the lines. " + finish;
        case MarkingKind::Hatching:
            return points == 0 ? "Click the first corner of the area to hatch. The stripes run at an angle to its first side."
                               : count() + "Click to add corners. To finish, click the first corner again or press Enter.";
        case MarkingKind::Text:
            if (settings_.marking.text.empty()) {
                return "Type the text in the panel first.";
            }
            return points == 0 ? "Click where the text starts." : "Now click to set its direction.";
        }
        break;
    case Tool::Select: {
        const size_t selected = document_ ? document_->Selection().size() : 0;
        if (selected == 0) {
            return "Click painted ground to select it. Ctrl+click to add to the selection.";
        }
        if (Is_(Gesture::MoveTexture)) {
            return "Release the button to keep the texture here.";
        }
        const std::string back = returnTool_ ? " Press Esc to draw another shape." : "";
        if (const PaintObject* const target = PointEditTarget_()) {
            const std::string holes = target->erasers.empty()
                                          ? ""
                                          : " Red outlines are holes. Change them the same way. A hole goes when you remove its last points.";
            if (Picked_()) {
                return "Drag a point or a curve handle to move it. Press Delete to remove the picked point. Press Esc to let go of it." + holes + back;
            }
            return std::string("Drag a point or a curve handle to move it. ") +
                   (WideLine_(*target) ? "Ctrl+click the middle of the line to add a point. " : "Click the outline to add a point. ") +
                   "Alt+drag a point to pull out curve handles. Alt+click a curved point to make it a corner. Alt+drag a curve handle to bend one side only. Click a point and press Delete to remove it. "
                   "Drag inside the shape to move it. Shift+drag to move its texture. Press Delete to remove the shape." + holes + back;
        }
        return "Drag to move. Shift+drag to move the texture. Press , or . to turn. Press - or = to scale. Press Ctrl+D to copy. Press Delete to remove." + back;
    }
    }
    return {};
}
