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

    float DistanceToSegment(const Vec2 p, const Vec2 a, const Vec2 b) {
        const float dx = b.x - a.x;
        const float dz = b.z - a.z;
        const float lengthSquared = dx * dx + dz * dz;
        const float t = lengthSquared > 0.0f ? std::clamp(((p.x - a.x) * dx + (p.z - a.z) * dz) / lengthSquared, 0.0f, 1.0f)
                                             : 0.0f;
        return Distance(p, {a.x + t * dx, a.z + t * dz});
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
    if (strokeActive_) {
        CancelShape_();  // stroke tools never have anchors
    }
    if ((dragging_ || pointDrag_ || handleDrag_ || textureDrag_) && document_) {
        document_->ShowSelectionMoved({});
    }
    dragging_ = false;
    textureDrag_ = false;
    pointDrag_.reset();
    handleDrag_.reset();
    penDrag_ = false;
    middleDrag_ = MiddleDrag::None;
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
        return settings_.drawStyle == DrawStyle::Freehand;
    case Tool::Marking:
    case Tool::Select:
        return false;
    }
    return false;
}

bool GroundPainterInputControl::UsesPen_() const {
    return settings_.drawStyle == DrawStyle::Pen &&
           (settings_.tool == Tool::Area || settings_.tool == Tool::Line ||
            (settings_.tool == Tool::Erase && !settings_.eraseWithBrush));
}

float GroundPainterInputControl::PickDistance_() const {
    return std::max(kPointPickDistance, snapReach_);
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
        return PenPath(points, anchorHandles_, {}, 0.0f, ClosesShape_());
    }
    return PathThrough(points, DrawsCurve_() ? PathStyle::Curved : PathStyle::Straight, ClosesShape_());
}

std::optional<size_t> GroundPainterInputControl::PickedPoint() const {
    const PaintObject* const target = PointEditTarget_();
    if (!target || target->id != pickedObject_ || pickedIndex_ >= target->shape.points.size()) {
        return std::nullopt;
    }
    return pickedIndex_;
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
    if (penDrag_) {
        return false;  // the handle goes where the mouse goes
    }
    if (pointDrag_) {
        return true;
    }
    switch (settings_.tool) {
    case Tool::Area:
    case Tool::Line:
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
    if (pointDrag_) {
        const std::vector<Vec2>& points = pointEdit_.shape.points;
        if (points.size() < 2) {
            return std::nullopt;
        }
        const size_t index = *pointDrag_;
        if (index > 0) {
            return points[index - 1];
        }
        return PointsClosed(pointEdit_) ? points.back() : points[1];
    }
    if (!anchors_.empty() && !strokeActive_) {
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

    if (settings_.snapPointsToGrid && !penDrag_ && (settings_.tool != Tool::Select || pointDrag_)) {
        // The other painters' grid: at least 1 m.
        const float step = std::max(settings_.gridStepMeters, 1.0f);
        cursor_ = {std::round(cursor_.x / step) * step, std::round(cursor_.z / step) * step};
    }
}

void GroundPainterInputControl::UpdateSnapIndex_() {
    const uint32_t leftOut = pointDrag_ ? pointEdit_.id : 0;
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
            dragging_ = true;
            dragStart_ = cursor_;
        }
        RefreshPreview_();
        return true;
    }

    // The eyedropper: Alt+click, or a click after "Copy look" in the panel.
    if (settings_.tool != Tool::Erase && (lookPickArmed_ || KeyHeld(VK_MENU))) {
        lookPickArmed_ = false;
        PickLook_();
        RefreshPreview_();
        return true;
    }

    if (UsesStroke_()) {
        strokeActive_ = true;
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
        penHandleBefore_ = anchorHandles_.back();
        penDrag_ = true;
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
    if (penDrag_) {
        penDrag_ = false;
        RefreshPreview_();
        return true;
    }
    if (strokeActive_) {
        FinishStroke_();
        return true;
    }
    if (pointDrag_ || handleDrag_) {
        FinishPointDrag_();
        return true;
    }
    if (textureDrag_) {
        FinishTextureDrag_();
        return true;
    }
    if (dragging_) {
        FinishDrag_();
        return true;
    }
    return false;
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
    if (middleDrag_ != MiddleDrag::None) {
        const float dx = static_cast<float>(x - middleStartX_);
        switch (middleDrag_) {
        case MiddleDrag::Brush:
            SetBrushRadius_(middleStartRadius_ * std::exp(dx * kBrushPerPixel));
            break;
        case MiddleDrag::Turn:
            middleAmount_ = dx * kTurnPerPixel;
            break;
        case MiddleDrag::Scale:
            middleAmount_ = std::exp(dx * kScalePerPixel);
            break;
        case MiddleDrag::None:
            break;
        }
        return true;
    }

    if (strokeActive_) {
        const float spacing = settings_.tool == Tool::Brush || settings_.tool == Tool::Erase
                                  ? std::max(0.25f, settings_.brushRadius * 0.25f)
                                  : kFreehandSpacing;
        if (stroke_.empty() || Distance(stroke_.back(), cursor_) >= spacing) {
            stroke_.push_back(cursor_);
        }
    }
    else if (penDrag_ && !anchors_.empty()) {
        const bool pulled = std::hypot(static_cast<float>(x - penPressX_), static_cast<float>(z - penPressZ_)) >= kPenDragPixels;
        const Vec2 anchor = anchors_.back();
        const Vec2 aim = KeyHeld(VK_SHIFT) ? SnapToAngle(anchor, cursor_, kAngleStep) : cursor_;
        anchorHandles_.back() = pulled ? Vec2{aim.x - anchor.x, aim.z - anchor.z} : penHandleBefore_;
    }
    else if (pointDrag_) {
        pointEdit_.shape.points[*pointDrag_] = cursor_;
    }
    else if (handleDrag_) {
        const Vec2 anchor = pointEdit_.shape.points[*handleDrag_];
        const Vec2 aim = KeyHeld(VK_SHIFT) ? SnapToAngle(anchor, cursor_, kAngleStep) : cursor_;
        pointEdit_.shape.handles[*handleDrag_] = {(aim.x - anchor.x) * handleSign_, (aim.z - anchor.z) * handleSign_};
    }
    return true;
}

void GroundPainterInputControl::ShowMove_() {
    moved_ = false;
    if (!document_) {
        return;
    }
    if (middleDrag_ == MiddleDrag::Turn) {
        TransformSelection_(middleAmount_, 1.0f, true);
    }
    else if (middleDrag_ == MiddleDrag::Scale) {
        TransformSelection_(0.0f, middleAmount_, true);
    }
    else if (pointDrag_ || handleDrag_) {
        document_->ShowChanged(pointEdit_);
    }
    else if (textureDrag_) {
        // A new style for each: no new cut.
        for (const PaintObject& object : TextureDragged_()) {
            document_->ShowChanged(object);
        }
    }
    else if (dragging_) {
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
            RefreshPreview_();
        }
        return true;
    case VK_DELETE:
        if (settings_.tool == Tool::Select && !document_->Selection().empty()) {
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
        RefreshPreview_();
        return true;
    case 'S':
        if (!KeyHeld(VK_SHIFT)) {
            return false;
        }
        settings_.snapPointsToGrid = !settings_.snapPointsToGrid;
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

bool GroundPainterInputControl::OnMouseDownM(const int32_t x, const int32_t z, const uint32_t /*modifiers*/) {
    if (!active_ || !document_) {
        return false;
    }
    UpdateCursor_(x, z);
    middleStartX_ = x;
    if (UsesBrush_()) {
        // Drag sideways to size the brush ([ and ]).
        middleDrag_ = MiddleDrag::Brush;
        middleStartRadius_ = settings_.brushRadius;
        return true;
    }
    if (settings_.tool == Tool::Select && !document_->Selection().empty() && !dragging_ && !pointDrag_) {
        // Drag sideways to turn the selection (, and .), with Shift to scale it (- and =).
        const Bounds bounds = document_->SelectionBounds();
        middlePivot_ = {(bounds.min.x + bounds.max.x) * 0.5f, (bounds.min.z + bounds.max.z) * 0.5f};
        middleDrag_ = KeyHeld(VK_SHIFT) ? MiddleDrag::Scale : MiddleDrag::Turn;
        middleAmount_ = middleDrag_ == MiddleDrag::Scale ? 1.0f : 0.0f;
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
    const MiddleDrag drag = std::exchange(middleDrag_, MiddleDrag::None);
    if (drag == MiddleDrag::Turn && middleAmount_ != 0.0f) {
        TransformSelection_(middleAmount_, 1.0f, false);
    }
    else if (drag == MiddleDrag::Scale && middleAmount_ != 1.0f) {
        TransformSelection_(0.0f, middleAmount_, false);
    }
    return drag != MiddleDrag::None;
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
        if (middleDrag_ == MiddleDrag::None) {
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
    object.look = settings_.look;
    ShapeSpec& shape = object.shape;
    shape.points = points;
    shape.pathStyle = curved ? PathStyle::Curved : PathStyle::Straight;
    if (UsesPen_()) {
        shape.handles = anchorHandles_;
    }
    shape.wear = settings_.tool == Tool::Erase ? 0.0f : settings_.wear;
    shape.roughness = settings_.tool == Tool::Erase || settings_.tool == Tool::Marking ? 0.0f : settings_.roughness;
    switch (settings_.tool) {
    case Tool::Line:
        shape.kind = ShapeKind::Line;
        shape.width = settings_.lineWidth;
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
    if (cursorValid_ && !penDrag_ && !points.empty() && Distance(points.back(), cursor_) > 0.01f) {
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
        document_->Erase(BuildArea(object));
    }
    else if (anchors_.size() >= MinimumPoints(object)) {
        LogAdded_(object, document_->Add(object));
    }
    else {
        LOG_INFO("Ground: not painted, the shape has {} of the {} points it needs", anchors_.size(), MinimumPoints(object));
    }
    CancelShape_();
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
    strokeActive_ = false;
    if (stroke_.empty() || !document_) {
        CancelShape_();
        return;
    }
    if (stroke_.back() != cursor_ && cursorValid_) {
        stroke_.push_back(cursor_);
    }

    if (settings_.tool == Tool::Erase) {
        document_->Erase(BrushStroke(stroke_, settings_.brushRadius));
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
        if (!object || object->locked || object->look.textures.empty() || !BuildUvMap(*object).empty()) {
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
    textureDrag_ = true;
    RefreshPreview_();
    return true;
}

void GroundPainterInputControl::FinishTextureDrag_() {
    textureDrag_ = false;
    if (std::hypot(cursor_.x - dragStart_.x, cursor_.z - dragStart_.z) >= 0.01f) {
        document_->Update(TextureDragged_());
    }
    document_->ShowSelectionMoved({});
    RefreshPreview_();
}

void GroundPainterInputControl::FinishDrag_() {
    dragging_ = false;
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
    return object && !object->locked && !object->hidden && HasEditablePoints(*object) ? object : nullptr;
}

bool GroundPainterInputControl::StartPointEdit_(const bool insert, const bool remove) {
    const PaintObject* const object = PointEditTarget_();
    if (!object) {
        return false;
    }
    const std::vector<Vec2>& points = object->shape.points;

    std::optional<size_t> nearest;
    for (size_t i = 0; i < points.size(); ++i) {
        if (Distance(points[i], cursor_) < PickDistance_() &&
            (!nearest || Distance(points[i], cursor_) < Distance(points[*nearest], cursor_))) {
            nearest = i;
        }
    }

    pointEdit_ = *object;
    if (remove) {
        // Alt+click takes a point out, as long as the shape keeps enough of them.
        if (!nearest || points.size() <= MinimumPoints(*object)) {
            return nearest.has_value();
        }
        EraseShapePoint(pointEdit_.shape, *nearest);
        pickedObject_ = 0;
        document_->Update({pointEdit_});
        return true;
    }
    if (!insert) {
        // The end of a curve handle, when it is nearer than any point.
        const std::vector<Vec2>& handles = object->shape.handles;
        float best = nearest ? Distance(points[*nearest], cursor_) : PickDistance_();
        std::optional<size_t> tip;
        for (size_t i = 0; i < std::min(handles.size(), points.size()); ++i) {
            for (const float sign : {1.0f, -1.0f}) {
                const float distance = Distance({points[i].x + handles[i].x * sign, points[i].z + handles[i].z * sign}, cursor_);
                if (handles[i] != Vec2{} && distance < best) {
                    best = distance;
                    tip = i;
                    handleSign_ = sign;
                }
            }
        }
        if (tip) {
            handleDrag_ = tip;
            pickedObject_ = object->id;
            pickedIndex_ = *tip;
            document_->ShowChanged(pointEdit_);
            return true;
        }
    }
    if (!nearest && insert) {
        // Ctrl+click on the path between two points adds one there.
        const size_t segments = PointsClosed(*object) ? points.size() : points.size() - 1;
        const size_t most = PointsFor(object->shape.marking.kind).maxPoints;
        if (object->shape.kind == ShapeKind::Marking && most > 0 && points.size() >= most) {
            return false;
        }
        for (size_t i = 0; i < segments; ++i) {
            if (DistanceToSegment(cursor_, points[i], points[(i + 1) % points.size()]) < PickDistance_()) {
                nearest = i + 1;
                InsertShapePoint(pointEdit_.shape, i + 1, cursor_);
                break;
            }
        }
    }
    if (!nearest) {
        return false;
    }
    pointDrag_ = nearest;
    pickedObject_ = object->id;
    pickedIndex_ = *nearest;
    document_->ShowChanged(pointEdit_);
    return true;
}

void GroundPainterInputControl::FinishPointDrag_() {
    pointDrag_.reset();
    handleDrag_.reset();
    document_->Update({pointEdit_});
    // Shows the object as the document has it, also when the change painted nothing.
    document_->ShowSelectionMoved({});
    RefreshPreview_();
}

void GroundPainterInputControl::CancelShape_() {
    anchors_.clear();
    anchorHandles_.clear();
    penDrag_ = false;
    stroke_.clear();
    strokeActive_ = false;
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
    std::vector<PaintOverlay::GroundGuide> guides;
    std::vector<Vec2> markers;
    float ring = 0.0f;
    uint32_t ringColor = kCursorPathColor;
    // Curve handles: a line through the point, and a marker on each end.
    const auto showHandles = [&](const std::vector<Vec2>& points, const std::vector<Vec2>& handles) {
        for (size_t i = 0; i < std::min(points.size(), handles.size()); ++i) {
            if (handles[i] != Vec2{}) {
                const Vec2 ahead{points[i].x + handles[i].x, points[i].z + handles[i].z};
                const Vec2 behind{points[i].x - handles[i].x, points[i].z - handles[i].z};
                guides.push_back({{behind, ahead}, false, kSnapGuideColor});
                markers.push_back(ahead);
                markers.push_back(behind);
            }
        }
    };

    if (document_) {
        if (settings_.tool == Tool::Select) {
            for (const uint32_t selected : document_->Selection()) {
                const Area area = (pointDrag_ || handleDrag_) && selected == pointEdit_.id ? BuildArea(pointEdit_)
                                                                                         : document_->AreaOf(selected);
                for (const Ring& outline : area) {
                    guides.push_back({outline, true, kSelectionColor});
                }
            }
            if (pointDrag_ || handleDrag_) {
                markers = pointEdit_.shape.points;
                showHandles(pointEdit_.shape.points, pointEdit_.shape.handles);
            }
            else if (const PaintObject* const target = PointEditTarget_()) {
                markers = target->shape.points;
                showHandles(target->shape.points, target->shape.handles);
                if (const auto picked = PickedPoint()) {
                    // A diamond round the point that the panel's corner radius is for.
                    const Vec2 p = target->shape.points[*picked];
                    const float kSize = std::max(0.3f, snapReach_ * 0.5f);
                    guides.push_back({{{p.x - kSize, p.z}, {p.x, p.z - kSize}, {p.x + kSize, p.z}, {p.x, p.z + kSize}}, true, kSnapPointColor});
                }
            }
            if (cursorValid_ && !dragging_) {
                if (const auto hover = document_->HitTest(cursor_); hover && !document_->IsSelected(*hover)) {
                    for (const Ring& outline : document_->AreaOf(*hover)) {
                        guides.push_back({outline, true, kHoverColor});
                    }
                }
            }
        }
        else if (strokeActive_ || !anchors_.empty()) {
            const bool eraser = settings_.tool == Tool::Erase;
            const PaintObject drawing = strokeActive_
                                            ? ObjectFrom_(stroke_, false)
                                            : ObjectFrom_(PointsWithCursor_(), DrawsCurve_());
            if (strokeActive_ && settings_.tool != Tool::Brush && !eraser) {
                // A freehand stroke shows as the path being drawn; it is smoothed on release.
                guides.push_back({stroke_, false, kPathColor});
                document_->ShowPreview(nullptr);
            }
            else if (eraser) {
                const Area area = strokeActive_ ? BrushStroke(stroke_, settings_.brushRadius) : BuildArea(drawing);
                for (const Ring& outline : area) {
                    guides.push_back({outline, true, kEraserColor});
                }
            }
            else {
                document_->ShowPreview(&drawing);
            }
            if (!strokeActive_) {
                guides.push_back({DrawingPath_(), ClosesShape_(), eraser ? kEraserColor : kPathColor});
                markers = anchors_;
                showHandles(anchors_, anchorHandles_);
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
    if (cursorValid_ && !penDrag_ && UsesPen_() && ClosesShape_() && anchors_.size() >= 3 &&
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
    if (!cursorValid_ || strokeActive_ || (!pointDrag_ && anchors_.empty())) {
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
    const std::vector<Vec2>& points = pointDrag_ ? pointEdit_.shape.points : anchors_;
    if (!pointDrag_ && points.size() >= 2) {
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
    if (!pointDrag_ && settings_.tool != Tool::Select) {
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
    if (settings_.tool != Tool::Select || pointDrag_ || !document_) {
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

bool GroundPainterInputControl::PickLook_() {
    const auto hit = document_ ? document_->HitTest(cursor_) : std::nullopt;
    const PaintObject* const object = hit ? document_->Find(*hit) : nullptr;
    if (!object) {
        return false;
    }
    settings_.look = object->look;
    settings_.wear = object->shape.wear;
    settings_.roughness = object->shape.kind == ShapeKind::Marking ? settings_.roughness : object->shape.roughness;
    ++lookPickCount_;
    return true;
}

std::string GroundPainterInputControl::StatusText() const {
    if (lookPickArmed_) {
        return "Click painted ground to copy its look.";
    }
    if (penDrag_ || handleDrag_) {
        return "Drag to shape the curve. Hold Shift to turn it in 15 degree steps. Release the button to keep it.";
    }
    const size_t points = anchors_.size();
    const auto count = [&] { return std::to_string(points) + (points == 1 ? " point. " : " points. "); };
    const std::string finish = "Press Enter or middle-click to finish. Press Backspace to remove the last point.";

    switch (settings_.tool) {
    case Tool::Area:
    case Tool::Line:
    case Tool::Erase: {
        const bool area = settings_.tool != Tool::Line;
        if (settings_.tool == Tool::Erase && settings_.eraseWithBrush) {
            return "Hold the left button and drag over painted ground to remove it. Press [ or ] to change the size.";
        }
        if (settings_.drawStyle == DrawStyle::Freehand) {
            return strokeActive_ ? "Release the button to finish." : area ? "Hold the left button and draw around the area."
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
        return strokeActive_ ? "Release the button to finish the stroke."
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
        if (textureDrag_) {
            return "Release the button to keep the texture here.";
        }
        if (PointEditTarget_()) {
            return "Drag the shape, its points or their curve handles to move them. Ctrl+click its outline to add a point. Alt+click a point to remove it. "
                   "Shift+drag to move its texture. Press Delete to remove the shape.";
        }
        return "Drag to move. Shift+drag to move the texture. Press , or . to turn. Press - or = to scale. Press Ctrl+D to copy. Press Delete to remove.";
    }
    }
    return {};
}
