#include "GroundPaintDocument.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "public/cIGZTerrainPaintService.h"
#include "rfl/cbor/read.hpp"
#include "rfl/cbor/write.hpp"

namespace Ground {
    namespace {
        constexpr uint32_t kDecalTextureType = 0x7AB50E44;
        constexpr uint32_t kDecalTextureGroup = 0x0986135E;
        constexpr size_t kMaxUndoSteps = 100;

        // The paint's style. area is what is painted, for the size of the worn patches.
        TerrainPaintStyle ToStyle(const PaintObject& object, const Area& area, const int32_t drawOrder, const bool hidden,
                                  const bool transient, const uint32_t whiteTexture) {
            const Look& look = object.look;
            TerrainPaintStyle style{};
            const size_t count = std::min<size_t>(look.textures.size(), kTerrainPaintMaxTextures);
            for (size_t i = 0; i < count; ++i) {
                style.textures[i].key = cGZPersistResourceKey(kDecalTextureType, kDecalTextureGroup,
                                                              look.textures[i].instance);
                style.textures[i].weight = look.textures[i].weight;
            }
            style.textureCount = static_cast<uint32_t>(count);
            if (count == 0) {
                style.textures[0].key = cGZPersistResourceKey(kDecalTextureType, kDecalTextureGroup, whiteTexture);
                style.textureCount = 1;
            }
            style.variationSeed = look.variationSeed;
            style.textureSizeMeters = std::max(0.1f, look.textureSizeMeters);
            style.textureRotationTurns = look.textureRotationDegrees / 360.0f;
            style.color = cS3DVector3(look.color[0], look.color[1], look.color[2]);
            style.opacity = look.opacity;
            style.featherMeters = look.featherMeters;
            style.drawOrder = drawOrder;
            style.enabled = !hidden;
            style.flags = transient ? kTerrainPaintFlagTransient : 0u;
            if (object.shape.wear > 0.0f) {
                style.wear = object.shape.wear;
                style.wearPatchMeters = WearPatchSize(area);
                style.wearSeed = object.shape.wearSeed;
            }
            return style;
        }

        // The shape as the service takes it: all rings' points in a row, their lengths, and the
        // UV map if the texture follows the shape.
        struct FlatShape {
            std::vector<TerrainPaintPoint> points;
            std::vector<uint32_t> ringSizes;
            std::vector<TerrainPaintUvVertex> uvMap;

            FlatShape(const Area& area, const std::vector<UvVertex>& uv) {
                for (const Ring& ring : area) {
                    for (const Vec2& p : ring) {
                        points.push_back({p.x, p.z});
                    }
                    ringSizes.push_back(static_cast<uint32_t>(ring.size()));
                }
                for (const UvVertex& corner : uv) {
                    uvMap.push_back({corner.x, corner.z, corner.u, corner.v});
                }
            }

            [[nodiscard]] TerrainPaintShape View() const {
                return {points.data(), ringSizes.data(), static_cast<uint32_t>(ringSizes.size()),
                        uvMap.empty() ? nullptr : uvMap.data(), static_cast<uint32_t>(uvMap.size())};
            }
        };

        bool Overlaps(const Bounds& a, const Bounds& b) {
            return a.min.x <= b.max.x && b.min.x <= a.max.x && a.min.z <= b.max.z && b.min.z <= a.max.z;
        }
    }

    GroundPaintDocument::GroundPaintDocument(const uint32_t whiteTextureInstance)
        : whiteTextureInstance_(whiteTextureInstance) {}

    GroundPaintDocument::~GroundPaintDocument() = default;

    void GroundPaintDocument::SetPaintService(cIGZTerrainPaintService* const service) {
        service_ = service;
        previewPaintId_ = 0;
    }

    void GroundPaintDocument::Reset() {
        objects_.clear();
        areas_.clear();
        selection_.clear();
        undo_.clear();
        redo_.clear();
        nextId_ = 1;
        previewPaintId_ = 0;
    }

    const PaintObject* GroundPaintDocument::Find(const uint32_t id) const {
        const auto it = std::ranges::find(objects_, id, &PaintObject::id);
        return it != objects_.end() ? &*it : nullptr;
    }

    PaintObject* GroundPaintDocument::FindMutable_(const uint32_t id) {
        const auto it = std::ranges::find(objects_, id, &PaintObject::id);
        return it != objects_.end() ? &*it : nullptr;
    }

    const Area& GroundPaintDocument::AreaOf(const uint32_t id) const {
        static const Area kNone{};
        const auto it = areas_.find(id);
        return it != areas_.end() ? it->second : kNone;
    }

    int32_t GroundPaintDocument::TopOrder_() const {
        return objects_.empty() ? 0 : objects_.back().drawOrder;
    }

    uint32_t GroundPaintDocument::Add(PaintObject object) {
        if (IsEmpty(BuildArea(object))) {
            return 0;
        }
        object.id = nextId_++;
        object.drawOrder = TopOrder_() + 1;
        object.paintId = 0;
        if (object.shape.wearSeed == 0) {
            object.shape.wearSeed = object.id * 2654435761u;
        }
        const uint32_t id = object.id;
        Commit_({}, {std::move(object)});
        return id;
    }

    void GroundPaintDocument::Update(std::vector<PaintObject> changed) {
        std::vector<PaintObject> before;
        for (const PaintObject& object : changed) {
            if (const PaintObject* current = Find(object.id)) {
                before.push_back(*current);
            }
        }
        std::erase_if(changed, [this](const PaintObject& object) { return Find(object.id) == nullptr; });
        if (!changed.empty()) {
            Commit_(std::move(before), std::move(changed));
        }
    }

    void GroundPaintDocument::Remove(const std::span<const uint32_t> ids) {
        std::vector<PaintObject> before;
        for (const uint32_t id : ids) {
            if (const PaintObject* current = Find(id)) {
                before.push_back(*current);
            }
        }
        if (!before.empty()) {
            Commit_(std::move(before), {});
        }
    }

    void GroundPaintDocument::Erase(const Area& eraser) {
        if (IsEmpty(eraser)) {
            return;
        }
        const Bounds eraserBounds = BoundsOf(eraser);
        std::vector<PaintObject> before;
        std::vector<PaintObject> after;
        for (const PaintObject& object : objects_) {
            if (object.hidden || object.locked || !Overlaps(eraserBounds, BoundsOf(AreaOf(object.id))) ||
                IsEmpty(Intersect(AreaOf(object.id), eraser))) {
                continue;
            }
            before.push_back(object);
            PaintObject erased = object;
            erased.erased = object.erased.empty() ? eraser : Unite(object.erased, eraser);
            if (IsEmpty(BuildArea(erased))) {
                continue;  // rubbed out completely: removed below
            }
            after.push_back(std::move(erased));
        }
        if (!before.empty()) {
            Commit_(std::move(before), std::move(after));
        }
    }

    void GroundPaintDocument::Reorder_(const uint32_t id, const int steps) {
        const auto it = std::ranges::find(objects_, id, &PaintObject::id);
        if (it == objects_.end() || steps == 0) {
            return;
        }
        const auto index = static_cast<int>(it - objects_.begin());
        const int target = std::clamp(index + steps, 0, static_cast<int>(objects_.size()) - 1);
        if (target == index) {
            return;
        }

        // The objects in between shift by one and the moved one takes the target's place.
        std::vector<PaintObject> before;
        std::vector<PaintObject> after;
        const int lo = std::min(index, target);
        const int hi = std::max(index, target);
        std::vector<int32_t> orders;
        for (int i = lo; i <= hi; ++i) {
            before.push_back(objects_[i]);
            orders.push_back(objects_[i].drawOrder);
        }
        std::vector<PaintObject> moved = before;
        if (target > index) {
            std::rotate(moved.begin(), moved.begin() + 1, moved.end());
        }
        else {
            std::rotate(moved.rbegin(), moved.rbegin() + 1, moved.rend());
        }
        for (size_t i = 0; i < moved.size(); ++i) {
            moved[i].drawOrder = orders[i];
        }
        after = std::move(moved);
        Commit_(std::move(before), std::move(after));
    }

    void GroundPaintDocument::MoveUp(const uint32_t id) { Reorder_(id, 1); }
    void GroundPaintDocument::MoveDown(const uint32_t id) { Reorder_(id, -1); }
    void GroundPaintDocument::MoveToTop(const uint32_t id) { Reorder_(id, static_cast<int>(objects_.size())); }
    void GroundPaintDocument::MoveToBottom(const uint32_t id) { Reorder_(id, -static_cast<int>(objects_.size())); }

    void GroundPaintDocument::Duplicate(const std::span<const uint32_t> ids, const Vec2 offset) {
        std::vector<PaintObject> copies;
        int32_t order = TopOrder_();
        // Bottom to top, so the copies keep their order.
        for (const PaintObject& object : objects_) {
            if (std::ranges::find(ids, object.id) == ids.end()) {
                continue;
            }
            PaintObject copy = object;
            copy.id = nextId_++;
            copy.drawOrder = ++order;
            copy.paintId = 0;
            copy.locked = false;
            TransformObject(copy, {.offset = offset});
            copies.push_back(std::move(copy));
        }
        if (copies.empty()) {
            return;
        }
        std::vector<uint32_t> selected;
        for (const PaintObject& copy : copies) {
            selected.push_back(copy.id);
        }
        Commit_({}, std::move(copies));
        selection_ = std::move(selected);
    }

    uint32_t GroundPaintDocument::Merge(const std::span<const uint32_t> ids) {
        std::vector<PaintObject> before;
        Area outline;
        for (const PaintObject& object : objects_) {
            if (object.locked || std::ranges::find(ids, object.id) == ids.end()) {
                continue;
            }
            before.push_back(object);
            outline = outline.empty() ? AreaOf(object.id) : Unite(outline, AreaOf(object.id));
        }
        if (before.size() < 2 || IsEmpty(outline)) {
            return 0;
        }

        // The topmost keeps its place, its look and its name.
        PaintObject merged = before.back();
        merged.shape = ShapeSpec{.kind = ShapeKind::Merged, .outline = std::move(outline)};
        merged.erased.clear();
        merged.hidden = false;
        const uint32_t id = merged.id;
        Commit_(std::move(before), {std::move(merged)});
        selection_ = {id};
        return id;
    }

    void GroundPaintDocument::Group(const std::span<const uint32_t> ids) {
        std::vector<PaintObject> changed;
        uint32_t group = 0;
        for (const PaintObject& object : objects_) {
            group = std::max(group, object.group);
        }
        ++group;
        for (const uint32_t id : ids) {
            if (const PaintObject* object = Find(id)) {
                changed.push_back(*object);
                changed.back().group = group;
            }
        }
        if (changed.size() >= 2) {
            Update(std::move(changed));
        }
    }

    void GroundPaintDocument::Ungroup(const std::span<const uint32_t> ids) {
        std::vector<PaintObject> changed;
        for (const uint32_t id : ids) {
            if (const PaintObject* object = Find(id); object && object->group != 0) {
                changed.push_back(*object);
                changed.back().group = 0;
            }
        }
        Update(std::move(changed));
    }

    void GroundPaintDocument::Transform(const std::span<const uint32_t> ids, const Placement& placement) {
        std::vector<PaintObject> changed;
        for (const uint32_t id : ids) {
            if (const PaintObject* object = Find(id); object && !object->locked) {
                changed.push_back(*object);
                TransformObject(changed.back(), placement);
            }
        }
        Update(std::move(changed));
    }

    bool GroundPaintDocument::Undo() {
        if (undo_.empty()) {
            return false;
        }
        Step step = std::move(undo_.back());
        undo_.pop_back();
        Apply_(step.after, step.before);
        redo_.push_back(std::move(step));
        return true;
    }

    bool GroundPaintDocument::Redo() {
        if (redo_.empty()) {
            return false;
        }
        Step step = std::move(redo_.back());
        redo_.pop_back();
        Apply_(step.before, step.after);
        undo_.push_back(std::move(step));
        return true;
    }

    void GroundPaintDocument::Commit_(std::vector<PaintObject> before, std::vector<PaintObject> after) {
        Apply_(before, after);
        undo_.push_back(Step{std::move(before), std::move(after)});
        if (undo_.size() > kMaxUndoSteps) {
            undo_.erase(undo_.begin());
        }
        redo_.clear();
    }

    void GroundPaintDocument::Apply_(const std::vector<PaintObject>& from, const std::vector<PaintObject>& to) {
        for (const PaintObject& object : from) {
            if (std::ranges::find(to, object.id, &PaintObject::id) == to.end()) {
                RemoveObject_(object.id);
            }
        }
        for (const PaintObject& object : to) {
            Upsert_(object);
        }
        SortByOrder_();
    }

    void GroundPaintDocument::Upsert_(PaintObject object) {
        nextId_ = std::max(nextId_, object.id + 1);
        if (PaintObject* current = FindMutable_(object.id)) {
            // Undo and redo hand in copies from before; the terrain paint stays the current one.
            object.paintId = current->paintId;
            *current = std::move(object);
            Sync_(*current);
            return;
        }
        object.paintId = 0;
        objects_.push_back(std::move(object));
        Sync_(objects_.back());
    }

    void GroundPaintDocument::RemoveObject_(const uint32_t id) {
        const auto it = std::ranges::find(objects_, id, &PaintObject::id);
        if (it == objects_.end()) {
            return;
        }
        RemovePaint_(it->paintId);
        areas_.erase(id);
        std::erase(selection_, id);
        objects_.erase(it);
    }

    void GroundPaintDocument::Sync_(PaintObject& object) {
        Area& area = areas_[object.id];
        area = BuildArea(object);
        if (!service_) {
            return;
        }
        if (IsEmpty(area)) {
            RemovePaint_(object.paintId);
            return;
        }

        const FlatShape shape(area, BuildUvMap(object));
        const TerrainPaintStyle style = ToStyle(object, area, object.drawOrder, object.hidden, false, whiteTextureInstance_);
        if (object.paintId != 0 && service_->SetPaintShape(TerrainPaintId{object.paintId}, shape.View()) &&
            service_->SetPaintStyle(TerrainPaintId{object.paintId}, &style, sizeof(style))) {
            return;
        }
        RemovePaint_(object.paintId);
        TerrainPaintId id{};
        if (service_->CreatePaint(shape.View(), &style, sizeof(style), &id)) {
            object.paintId = id.value;
        }
    }

    void GroundPaintDocument::RemovePaint_(uint32_t& paintId) {
        if (paintId != 0 && service_) {
            service_->RemovePaint(TerrainPaintId{paintId});
        }
        paintId = 0;
    }

    void GroundPaintDocument::SortByOrder_() {
        std::ranges::stable_sort(objects_, {}, &PaintObject::drawOrder);
    }

    std::optional<uint32_t> GroundPaintDocument::HitTest(const Vec2 point) const {
        for (auto it = objects_.rbegin(); it != objects_.rend(); ++it) {
            if (!it->hidden && Contains(AreaOf(it->id), point)) {
                return it->id;
            }
        }
        return std::nullopt;
    }

    bool GroundPaintDocument::IsSelected(const uint32_t id) const {
        return std::ranges::find(selection_, id) != selection_.end();
    }

    void GroundPaintDocument::Select(const uint32_t id, const bool addToSelection) {
        if (!addToSelection) {
            selection_.clear();
        }
        const PaintObject* const picked = Find(id);
        if (!picked) {
            return;
        }
        for (const PaintObject& object : objects_) {
            const bool inGroup = object.id == id || (picked->group != 0 && object.group == picked->group);
            if (inGroup && !IsSelected(object.id)) {
                selection_.push_back(object.id);
            }
        }
    }

    void GroundPaintDocument::ClearSelection() {
        selection_.clear();
    }

    void GroundPaintDocument::ShowPreview(const PaintObject* const object) {
        if (!service_) {
            return;
        }
        const Area area = object ? BuildArea(*object) : Area{};
        if (IsEmpty(area)) {
            RemovePaint_(previewPaintId_);
            return;
        }

        const FlatShape shape(area, BuildUvMap(*object));
        const TerrainPaintStyle style = ToStyle(*object, area, TopOrder_() + 1, false, true, whiteTextureInstance_);
        if (previewPaintId_ != 0 && service_->SetPaintShape(TerrainPaintId{previewPaintId_}, shape.View()) &&
            service_->SetPaintStyle(TerrainPaintId{previewPaintId_}, &style, sizeof(style))) {
            return;
        }
        RemovePaint_(previewPaintId_);
        TerrainPaintId id{};
        if (service_->CreatePaint(shape.View(), &style, sizeof(style), &id)) {
            previewPaintId_ = id.value;
        }
    }

    void GroundPaintDocument::ShowSelectionMoved(const Vec2 offset) {
        ShowSelectionTransformed({.offset = offset});
    }

    void GroundPaintDocument::ShowSelectionTransformed(const Placement& placement) {
        if (!service_) {
            return;
        }
        const bool moved = placement.rotationRadians != 0.0f || placement.scale != 1.0f ||
                           placement.offset.x != 0.0f || placement.offset.z != 0.0f;
        for (const uint32_t id : selection_) {
            const PaintObject* const object = Find(id);
            if (!object || object->paintId == 0) {
                continue;
            }
            if (!moved) {
                // As it is, look included: a change shown with ShowChanged is undone too.
                const FlatShape shape(AreaOf(id), BuildUvMap(*object));
                const TerrainPaintStyle style =
                    ToStyle(*object, AreaOf(id), object->drawOrder, object->hidden, false, whiteTextureInstance_);
                service_->SetPaintShape(TerrainPaintId{object->paintId}, shape.View());
                service_->SetPaintStyle(TerrainPaintId{object->paintId}, &style, sizeof(style));
                continue;
            }
            std::vector<UvVertex> uvMap = BuildUvMap(*object);
            if (!uvMap.empty()) {
                // The texture moves along with the line.
                PaintObject moved = *object;
                TransformObject(moved, placement);
                uvMap = BuildUvMap(moved);
            }
            const FlatShape shape(Transformed(AreaOf(id), placement.pivot, placement.rotationRadians, placement.scale,
                                              placement.offset),
                                  uvMap);
            service_->SetPaintShape(TerrainPaintId{object->paintId}, shape.View());
        }
    }

    void GroundPaintDocument::ShowChanged(const PaintObject& object) {
        const PaintObject* const current = Find(object.id);
        if (!service_ || !current || current->paintId == 0) {
            return;
        }
        const Area area = BuildArea(object);
        if (IsEmpty(area)) {
            return;
        }
        const FlatShape shape(area, BuildUvMap(object));
        const TerrainPaintStyle style = ToStyle(object, area, current->drawOrder, current->hidden, false, whiteTextureInstance_);
        service_->SetPaintShape(TerrainPaintId{current->paintId}, shape.View());
        service_->SetPaintStyle(TerrainPaintId{current->paintId}, &style, sizeof(style));
    }

    Bounds GroundPaintDocument::SelectionBounds() const {
        Area all;
        for (const uint32_t id : selection_) {
            const Area& area = AreaOf(id);
            all.insert(all.end(), area.begin(), area.end());
        }
        return BoundsOf(all);
    }

    std::vector<char> GroundPaintDocument::Save() const {
        return rfl::cbor::write(objects_);
    }

    bool GroundPaintDocument::Load(const std::span<const char> bytes) {
        // Fields added later are left at their defaults in older saves.
        auto result = rfl::cbor::read<std::vector<PaintObject>, rfl::DefaultIfMissing>(bytes);
        if (!result) {
            return false;
        }
        Reset();
        objects_ = std::move(*result);
        SortByOrder_();
        for (const PaintObject& object : objects_) {
            nextId_ = std::max(nextId_, object.id + 1);
            areas_[object.id] = BuildArea(object);
        }
        return true;
    }

    void GroundPaintDocument::Restore() {
        TerrainPaintStyle existing{};
        for (PaintObject& object : objects_) {
            // The service saved the paint with the city: keep it, it is the same one.
            if (service_ && object.paintId != 0 &&
                service_->GetPaintStyle(TerrainPaintId{object.paintId}, &existing, sizeof(existing))) {
                continue;
            }
            object.paintId = 0;
            Sync_(object);
        }
    }
}
