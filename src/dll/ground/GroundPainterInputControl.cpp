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

    constexpr uint32_t kPathColor = 0xE0FFFFFF;
    constexpr uint32_t kCursorPathColor = 0xC0FFD700;
    constexpr uint32_t kAnchorColor = 0xF0FFD700;
    constexpr uint32_t kEraserColor = 0xE0FF4040;
    constexpr uint32_t kSelectionColor = 0xF040C0FF;
    constexpr uint32_t kHoverColor = 0x9040C0FF;

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
    if ((dragging_ || pointDrag_ || textureDrag_) && document_) {
        document_->ShowSelectionMoved({});
    }
    dragging_ = false;
    textureDrag_ = false;
    pointDrag_.reset();
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
    if (settings_.snapPointsToGrid && (settings_.tool != Tool::Select || pointDrag_)) {
        // The other painters' grid: at least 1 m.
        const float step = std::max(settings_.gridStepMeters, 1.0f);
        cursor_ = {std::round(cursor_.x / step) * step, std::round(cursor_.z / step) * step};
    }
    return true;
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

    if (ClosesShape_() && anchors_.size() >= 3 && Distance(cursor_, anchors_.front()) < kCloseDistance) {
        Commit_();
        return true;
    }
    if (anchors_.empty() || Distance(cursor_, anchors_.back()) > 0.01f) {
        anchors_.push_back(cursor_);
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
    if (strokeActive_) {
        FinishStroke_();
        return true;
    }
    if (pointDrag_) {
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
    else if (pointDrag_) {
        pointEdit_.shape.points[*pointDrag_] = cursor_;
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
    else if (pointDrag_) {
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
    if (cursorValid_ && !points.empty() && Distance(points.back(), cursor_) > 0.01f) {
        points.push_back(cursor_);
    }
    return points;
}

void GroundPainterInputControl::Commit_() {
    if (!document_ || anchors_.empty()) {
        return;
    }
    const bool curved = settings_.drawStyle != DrawStyle::Clicks;
    const PaintObject object = ObjectFrom_(anchors_, curved);
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
        if (Distance(points[i], cursor_) < kPointPickDistance &&
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
        pointEdit_.shape.points.erase(pointEdit_.shape.points.begin() + static_cast<std::ptrdiff_t>(*nearest));
        document_->Update({pointEdit_});
        return true;
    }
    if (!nearest && insert) {
        // Ctrl+click on the path between two points adds one there.
        const size_t segments = PointsClosed(*object) ? points.size() : points.size() - 1;
        const size_t most = PointsFor(object->shape.marking.kind).maxPoints;
        if (object->shape.kind == ShapeKind::Marking && most > 0 && points.size() >= most) {
            return false;
        }
        for (size_t i = 0; i < segments; ++i) {
            if (DistanceToSegment(cursor_, points[i], points[(i + 1) % points.size()]) < kPointPickDistance) {
                nearest = i + 1;
                pointEdit_.shape.points.insert(pointEdit_.shape.points.begin() + static_cast<std::ptrdiff_t>(i + 1), cursor_);
                break;
            }
        }
    }
    if (!nearest) {
        return false;
    }
    pointDrag_ = nearest;
    document_->ShowChanged(pointEdit_);
    return true;
}

void GroundPainterInputControl::FinishPointDrag_() {
    pointDrag_.reset();
    document_->Update({pointEdit_});
    // Shows the object as the document has it, also when the change painted nothing.
    document_->ShowSelectionMoved({});
    RefreshPreview_();
}

void GroundPainterInputControl::CancelShape_() {
    anchors_.clear();
    stroke_.clear();
    strokeActive_ = false;
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

    if (document_) {
        if (settings_.tool == Tool::Select) {
            for (const uint32_t selected : document_->Selection()) {
                const Area area = pointDrag_ && selected == pointEdit_.id ? BuildArea(pointEdit_) : document_->AreaOf(selected);
                for (const Ring& outline : area) {
                    guides.push_back({outline, true, kSelectionColor});
                }
            }
            if (pointDrag_) {
                markers = pointEdit_.shape.points;
            }
            else if (const PaintObject* const target = PointEditTarget_()) {
                markers = target->shape.points;
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
                                            : ObjectFrom_(PointsWithCursor_(), settings_.drawStyle != DrawStyle::Clicks);
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
                const std::vector<Vec2> path = PathThrough(
                    PointsWithCursor_(), settings_.drawStyle != DrawStyle::Clicks ? PathStyle::Curved : PathStyle::Straight,
                    ClosesShape_());
                guides.push_back({path, ClosesShape_(), eraser ? kEraserColor : kPathColor});
                markers = anchors_;
            }
        }
    }

    if (cursorValid_ && (settings_.tool == Tool::Brush || (settings_.tool == Tool::Erase && settings_.eraseWithBrush))) {
        ring = settings_.brushRadius;
        ringColor = settings_.tool == Tool::Erase ? kEraserColor : kCursorPathColor;
    }
    overlay_.BuildGroundPreview(guides, markers, kAnchorColor, cursorValid_, cursor_, ring, ringColor, settings_.showGrid,
                                settings_.gridStepMeters, Terrain_());
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
    const size_t points = anchors_.size();
    const auto count = [&] { return std::to_string(points) + (points == 1 ? " point. " : " points. "); };
    const std::string finish = "Enter or a middle click finishes, Backspace removes the last point.";

    switch (settings_.tool) {
    case Tool::Area:
    case Tool::Line:
    case Tool::Erase: {
        const bool area = settings_.tool != Tool::Line;
        if (settings_.tool == Tool::Erase && settings_.eraseWithBrush) {
            return "Hold the left button and drag over painted ground to rub it out. [ and ] change the size.";
        }
        if (settings_.drawStyle == DrawStyle::Freehand) {
            return strokeActive_ ? "Let go to finish." : area ? "Hold the left button and draw round the area."
                                                              : "Hold the left button and draw the line.";
        }
        if (points == 0) {
            return area ? "Click in the city to place the first corner." : "Click in the city where the line starts.";
        }
        return count() + (area ? "Click to add corners; click the first one again or press Enter to finish. "
                               : "Click to continue the line. ") +
               finish;
    }
    case Tool::Brush:
        return strokeActive_ ? "Let go to finish the stroke."
                             : "Hold the left button and drag to paint. [ and ] or a middle-drag change the size.";
    case Tool::Marking:
        switch (settings_.marking.kind) {
        case MarkingKind::ParkingRow:
        case MarkingKind::CarPark:
            return points == 0 ? "Click where the front of the row starts. The bays go on the left of the direction you click in."
                               : count() + "Click along the front of the row. " + finish;
        case MarkingKind::Zebra:
            return points == 0 ? "Click one kerb, where the crossing starts." : "Now click the other kerb.";
        case MarkingKind::Arrow:
            return points == 0 ? "Click where the tail of the arrow goes." : "Now click where its tip goes.";
        case MarkingKind::DoubleLine:
            return points == 0 ? "Click where the lines start." : count() + "Click to continue the lines. " + finish;
        case MarkingKind::Hatching:
            return points == 0 ? "Click the first corner of the area to hatch. The stripes run at an angle to its first side."
                               : count() + "Click to add corners; click the first one again or press Enter to finish.";
        case MarkingKind::Text:
            if (settings_.marking.text.empty()) {
                return "Type the text in the panel first.";
            }
            return points == 0 ? "Click where the text starts." : "Now click which way it runs.";
        }
        break;
    case Tool::Select: {
        const size_t selected = document_ ? document_->Selection().size() : 0;
        if (selected == 0) {
            return "Click painted ground to select it. Ctrl+click adds to the selection.";
        }
        if (textureDrag_) {
            return "Let go to leave the texture here.";
        }
        if (PointEditTarget_()) {
            return "Drag it to move it, or drag its points. Ctrl+click its outline adds a point, Alt+click a point removes it. "
                   "Shift+drag moves its texture. Delete removes it.";
        }
        return "Drag to move, Shift+drag to move the texture. , and . turn, - and = scale, Ctrl+D copies, Delete removes.";
    }
    }
    return {};
}
