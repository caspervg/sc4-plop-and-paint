#include "GroundPaintDocument.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

#include "public/cIGZTerrainPaintService.h"
#include "rfl/cbor/read.hpp"
#include "rfl/cbor/write.hpp"

namespace Ground {
    namespace {
        constexpr uint32_t kDecalTextureType = 0x7AB50E44;
        constexpr uint32_t kDecalTextureGroup = 0x0986135E;
        constexpr size_t kMaxUndoSteps = 100;

        TerrainPaintLayer ToServiceLayer(const PaintLayer layer) {
            switch (layer) {
            case PaintLayer::UnderLots:
                return TerrainPaintLayer::UnderLots;
            case PaintLayer::OverShadows:
                return TerrainPaintLayer::OverShadows;
            case PaintLayer::OverLots:
            default:
                return TerrainPaintLayer::OverLots;
            }
        }

        // The paint's style. area is what is painted, for the size of the worn patches. part 0 is
        // the whole area; with texture patches, part i is the patches of texture i.
        TerrainPaintStyle ToStyle(const PaintObject& object, const Area& area, const int32_t drawOrder, const bool hidden,
                                  const bool transient, const uint32_t whiteTexture, const size_t part = 0) {
            const Look& look = object.look;
            TerrainPaintStyle style{};
            const bool patches = UsesPatches(look);
            const size_t count = patches ? 1 : std::min<size_t>(look.textures.size(), kTerrainPaintMaxTextures);
            for (size_t i = 0; i < count; ++i) {
                const TextureChoice& choice = look.textures[patches ? part : i];
                style.textures[i].key = cGZPersistResourceKey(kDecalTextureType, kDecalTextureGroup, choice.instance);
                style.textures[i].weight = choice.weight;
            }
            style.textureCount = static_cast<uint32_t>(count);
            if (count == 0) {
                style.textures[0].key = cGZPersistResourceKey(kDecalTextureType, kDecalTextureGroup, whiteTexture);
                style.textureCount = 1;
            }
            style.variationSeed = look.variationSeed;
            style.textureSizeMeters = std::max(0.1f, look.textureSizeMeters);
            style.textureRotationTurns = look.textureRotationDegrees / 360.0f;
            style.textureOffsetX = look.textureOffset[0];
            style.textureOffsetZ = look.textureOffset[1];
            style.color = cS3DVector3(look.color[0], look.color[1], look.color[2]);
            style.opacity = look.opacity;
            // Patches blend into what lies under them; the outer soft edge is the area's.
            style.featherMeters = part == 0 ? look.featherMeters : std::max(look.featherMeters, look.patchBlendMeters);
            style.drawOrder = drawOrder;
            style.depthOffset = look.depthOffset;
            style.layer = ToServiceLayer(look.layer);
            style.enabled = !hidden;
            style.flags = transient ? kTerrainPaintFlagTransient : 0u;
            if (object.shape.wear > 0.0f) {
                style.wear = object.shape.wear;
                style.wearPatchMeters = WearPatchSize(area);
                style.wearSeed = object.shape.wearSeed + static_cast<uint32_t>(part);
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

        // The patches of a texture can come out empty, where the noise leaves none; the service
        // takes no empty shape, so they are made as a tiny invisible-sized triangle instead of
        // being left out, which would shift the ids of the parts after them.
        const Area kNothing{{{0.0f, 0.0f}, {0.001f, 0.0f}, {0.0f, 0.001f}}};
    }

    GroundPaintDocument::GroundPaintDocument(const uint32_t whiteTextureInstance)
        : whiteTextureInstance_(whiteTextureInstance) {}

    GroundPaintDocument::~GroundPaintDocument() = default;

    void GroundPaintDocument::SetPaintService(cIGZTerrainPaintService* const service) {
        service_ = service;
        previewPaintId_ = 0;
        previewPatchIds_.clear();
        previewObject_.reset();
    }

    void GroundPaintDocument::Reset() {
        objects_.clear();
        built_.clear();
        shownShape_.clear();
        shownStyle_.clear();
        selection_.clear();
        undo_.clear();
        redo_.clear();
        nextId_ = 1;
        previewPaintId_ = 0;
        previewPatchIds_.clear();
        previewObject_.reset();
        ++revision_;
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
        const auto it = built_.find(id);
        return it != built_.end() ? it->second.area : kNone;
    }

    Bounds GroundPaintDocument::BoundsOf(const uint32_t id) const {
        const auto it = built_.find(id);
        return it != built_.end() ? it->second.bounds : Bounds{};
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
        object.patchPaintIds.clear();
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
        const Bounds eraserBounds = Ground::BoundsOf(eraser);
        std::vector<PaintObject> before;
        std::vector<PaintObject> after;
        for (const PaintObject& object : objects_) {
            if (object.hidden || object.locked || !Overlaps(eraserBounds, BoundsOf(object.id)) ||
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
            copy.patchPaintIds.clear();
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

    uint32_t GroundPaintDocument::AddBorder(const std::span<const uint32_t> ids, const float width,
                                            const BorderSide side) {
        Area all;
        const PaintObject* top = nullptr;
        for (const PaintObject& object : objects_) {
            if (std::ranges::find(ids, object.id) == ids.end()) {
                continue;
            }
            all = all.empty() ? AreaOf(object.id) : Unite(all, AreaOf(object.id));
            top = &object;
        }
        if (!top) {
            return 0;
        }
        Area border = BorderOf(all, width, side);
        if (IsEmpty(border)) {
            return 0;
        }
        PaintObject object;
        object.name = "Border of " + (top->name.empty() ? std::string(ShapeKindName(top->shape.kind)) + " " +
                                                               std::to_string(top->id)
                                                         : top->name);
        object.shape = ShapeSpec{.kind = ShapeKind::Merged, .outline = std::move(border)};
        object.look = top->look;
        const uint32_t id = Add(std::move(object));
        if (id != 0) {
            selection_ = {id};
        }
        return id;
    }

    void GroundPaintDocument::Repeat(const std::span<const uint32_t> ids, const int columns, const int rows,
                                     const Vec2 columnStep, const Vec2 rowStep) {
        std::vector<const PaintObject*> originals;
        for (const PaintObject& object : objects_) {
            if (std::ranges::find(ids, object.id) != ids.end()) {
                originals.push_back(&object);
            }
        }
        if (originals.empty()) {
            return;
        }
        // Groups are copied as groups: each copy of a group gets a group of its own.
        uint32_t nextGroup = 0;
        for (const PaintObject& object : objects_) {
            nextGroup = std::max(nextGroup, object.group);
        }
        std::vector<PaintObject> copies;
        std::vector<uint32_t> selected(ids.begin(), ids.end());
        int32_t order = TopOrder_();
        for (int row = 0; row < std::max(1, rows); ++row) {
            for (int column = 0; column < std::max(1, columns); ++column) {
                if (row == 0 && column == 0) {
                    continue;
                }
                if (copies.size() + originals.size() > kMaxRepeats) {
                    break;
                }
                const Vec2 offset{columnStep.x * static_cast<float>(column) + rowStep.x * static_cast<float>(row),
                                  columnStep.z * static_cast<float>(column) + rowStep.z * static_cast<float>(row)};
                std::unordered_map<uint32_t, uint32_t> groups;
                for (const PaintObject* original : originals) {
                    PaintObject copy = *original;
                    copy.id = nextId_++;
                    copy.drawOrder = ++order;
                    copy.paintId = 0;
                    copy.patchPaintIds.clear();
                    copy.locked = false;
                    if (copy.group != 0) {
                        const auto [it, added] = groups.try_emplace(copy.group, nextGroup + 1);
                        nextGroup += added ? 1 : 0;
                        copy.group = it->second;
                    }
                    TransformObject(copy, {.offset = offset});
                    selected.push_back(copy.id);
                    copies.push_back(std::move(copy));
                }
            }
        }
        if (copies.empty()) {
            return;
        }
        Commit_({}, std::move(copies));
        selection_ = std::move(selected);
    }

    void GroundPaintDocument::Mirror(const std::span<const uint32_t> ids, const Vec2 pivot, const float axisRadians,
                                     const bool copy) {
        std::vector<PaintObject> mirrored;
        int32_t order = TopOrder_();
        for (const PaintObject& object : objects_) {
            if (std::ranges::find(ids, object.id) == ids.end() || (!copy && object.locked)) {
                continue;
            }
            PaintObject changed = object;
            MirrorObject(changed, pivot, axisRadians);
            if (copy) {
                changed.id = nextId_++;
                changed.drawOrder = ++order;
                changed.paintId = 0;
                changed.patchPaintIds.clear();
                changed.locked = false;
                changed.group = 0;
            }
            mirrored.push_back(std::move(changed));
        }
        if (mirrored.empty()) {
            return;
        }
        if (!copy) {
            Update(std::move(mirrored));
            return;
        }
        std::vector<uint32_t> selected;
        for (const PaintObject& object : mirrored) {
            selected.push_back(object.id);
        }
        Commit_({}, std::move(mirrored));
        selection_ = std::move(selected);
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
        ++revision_;
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
            object.patchPaintIds = current->patchPaintIds;
            if (SameShape(object, *current) && built_.contains(object.id)) {
                // Only the look, the order or the name changed: a new style does.
                *current = std::move(object);
                shownStyle_.erase(current->id);
                Restyle_(*current, AreaOf(current->id), current->paintId, current->patchPaintIds, current->drawOrder,
                         current->hidden, false);
                return;
            }
            *current = std::move(object);
            Sync_(*current);
            return;
        }
        object.paintId = 0;
        object.patchPaintIds.clear();
        objects_.push_back(std::move(object));
        Sync_(objects_.back());
    }

    void GroundPaintDocument::RemoveObject_(const uint32_t id) {
        const auto it = std::ranges::find(objects_, id, &PaintObject::id);
        if (it == objects_.end()) {
            return;
        }
        RemovePaints_(it->paintId, it->patchPaintIds);
        built_.erase(id);
        shownShape_.erase(id);
        shownStyle_.erase(id);
        std::erase(selection_, id);
        objects_.erase(it);
    }

    void GroundPaintDocument::Sync_(PaintObject& object) {
        shownShape_.erase(object.id);
        shownStyle_.erase(object.id);
        Built& built = built_[object.id];
        built.area = BuildArea(object);
        built.bounds = Ground::BoundsOf(built.area);
        built.patches = BuildPatches(object, built.area, kPatchCells);
        if (!service_) {
            return;
        }
        if (IsEmpty(built.area)) {
            RemovePaints_(object.paintId, object.patchPaintIds);
            return;
        }
        ShowParts_(object.paintId, object.patchPaintIds, object, built.area, built.patches, BuildUvMap(object),
                   object.drawOrder, object.hidden, false);
    }

    void GroundPaintDocument::ShowParts_(uint32_t& paintId, std::vector<uint32_t>& patchIds, const PaintObject& object,
                                         const Area& area, const std::vector<Area>& patches,
                                         const std::vector<UvVertex>& uvMap, const int32_t drawOrder, const bool hidden,
                                         const bool transient) {
        const auto shapeOf = [&](const size_t part) {
            const Area& partArea = part == 0 ? area : patches[part - 1];
            return FlatShape(IsEmpty(partArea) ? kNothing : partArea, uvMap);
        };
        const auto styleOf = [&](const size_t part) {
            return ToStyle(object, area, drawOrder, hidden, transient, whiteTextureInstance_, part);
        };
        const auto update = [&](const uint32_t id, const size_t part) {
            const FlatShape shape = shapeOf(part);
            const TerrainPaintStyle style = styleOf(part);
            return id != 0 && service_->SetPaintShape(TerrainPaintId{id}, shape.View()) &&
                   service_->SetPaintStyle(TerrainPaintId{id}, &style, sizeof(style));
        };

        bool updated = patchIds.size() == patches.size() && update(paintId, 0);
        for (size_t i = 0; updated && i < patches.size(); ++i) {
            updated = update(patchIds[i], i + 1);
        }
        if (updated) {
            return;
        }
        // Made again, in order: equal draw orders draw in the order the paints were made.
        RemovePaints_(paintId, patchIds);
        for (size_t part = 0; part <= patches.size(); ++part) {
            const FlatShape shape = shapeOf(part);
            const TerrainPaintStyle style = styleOf(part);
            TerrainPaintId id{};
            if (!service_->CreatePaint(shape.View(), &style, sizeof(style), &id)) {
                RemovePaints_(paintId, patchIds);
                return;
            }
            (part == 0 ? paintId : patchIds.emplace_back()) = id.value;
        }
    }

    void GroundPaintDocument::Restyle_(const PaintObject& object, const Area& area, const uint32_t paintId,
                                       const std::vector<uint32_t>& patchIds, const int32_t drawOrder,
                                       const bool hidden, const bool transient) {
        if (!service_ || paintId == 0) {
            return;
        }
        const auto restyle = [&](const uint32_t id, const size_t part) {
            const TerrainPaintStyle style = ToStyle(object, area, drawOrder, hidden, transient, whiteTextureInstance_, part);
            service_->SetPaintStyle(TerrainPaintId{id}, &style, sizeof(style));
        };
        restyle(paintId, 0);
        for (size_t i = 0; i < patchIds.size(); ++i) {
            restyle(patchIds[i], i + 1);
        }
    }

    void GroundPaintDocument::RemovePaint_(uint32_t& paintId) {
        if (paintId != 0 && service_) {
            service_->RemovePaint(TerrainPaintId{paintId});
        }
        paintId = 0;
    }

    void GroundPaintDocument::RemovePaints_(uint32_t& paintId, std::vector<uint32_t>& patchIds) {
        RemovePaint_(paintId);
        for (uint32_t& id : patchIds) {
            RemovePaint_(id);
        }
        patchIds.clear();
    }

    void GroundPaintDocument::SortByOrder_() {
        std::ranges::stable_sort(objects_, {}, &PaintObject::drawOrder);
    }

    std::optional<uint32_t> GroundPaintDocument::HitTest(const Vec2 point) const {
        for (auto it = objects_.rbegin(); it != objects_.rend(); ++it) {
            const auto built = built_.find(it->id);
            if (!it->hidden && built != built_.end() && Contains(built->second.bounds, point) &&
                Contains(built->second.area, point)) {
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
        if (object && previewPaintId_ != 0 && previewObject_ && SameShape(*object, *previewObject_) &&
            UsesPatches(object->look) == UsesPatches(previewObject_->look)) {
            // The same shape in a new look, or nothing new at all: no new cut.
            if (!(object->look == previewObject_->look) || object->shape.wear != previewObject_->shape.wear) {
                Restyle_(*object, previewArea_, previewPaintId_, previewPatchIds_, TopOrder_() + 1, false, true);
                previewObject_ = *object;
            }
            return;
        }
        previewObject_.reset();
        previewArea_ = object ? BuildArea(*object) : Area{};
        if (IsEmpty(previewArea_)) {
            RemovePaints_(previewPaintId_, previewPatchIds_);
            return;
        }
        // Coarser patches: the preview is cut again as the mouse moves.
        ShowParts_(previewPaintId_, previewPatchIds_, *object, previewArea_,
                   BuildPatches(*object, previewArea_, kPreviewPatchCells), BuildUvMap(*object), TopOrder_() + 1, false,
                   true);
        if (previewPaintId_ != 0) {
            previewObject_ = *object;
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
            PaintObject* const object = FindMutable_(id);
            const auto built = built_.find(id);
            if (!object || object->paintId == 0 || built == built_.end()) {
                continue;
            }
            if (!moved) {
                // As it is, look included: a change shown with ShowChanged is undone too. Only
                // what was shown changed is put back, so letting go of a drag cuts nothing twice.
                if (shownShape_.erase(id) > 0) {
                    shownStyle_.erase(id);
                    ShowParts_(object->paintId, object->patchPaintIds, *object, built->second.area,
                               built->second.patches, BuildUvMap(*object), object->drawOrder, object->hidden, false);
                }
                else if (shownStyle_.erase(id) > 0) {
                    Restyle_(*object, built->second.area, object->paintId, object->patchPaintIds, object->drawOrder,
                             object->hidden, false);
                }
                continue;
            }
            shownShape_.insert(id);
            const auto move = [&](const Area& area) {
                return Transformed(area, placement.pivot, placement.rotationRadians, placement.scale, placement.offset);
            };
            std::vector<UvVertex> uvMap = BuildUvMap(*object);
            if (!uvMap.empty()) {
                // The texture moves along with the line.
                PaintObject moved = *object;
                TransformObject(moved, placement);
                uvMap = BuildUvMap(moved);
            }
            // Only the shapes: patches move along with the area, as they will once it is moved.
            const auto reshape = [&](const uint32_t paintId, const Area& area) {
                if (paintId != 0 && !IsEmpty(area)) {
                    const FlatShape shape(move(area), uvMap);
                    service_->SetPaintShape(TerrainPaintId{paintId}, shape.View());
                }
            };
            reshape(object->paintId, built->second.area);
            for (size_t i = 0; i < object->patchPaintIds.size() && i < built->second.patches.size(); ++i) {
                reshape(object->patchPaintIds[i], built->second.patches[i]);
            }
        }
    }

    void GroundPaintDocument::ShowChanged(const PaintObject& object) {
        PaintObject* const current = FindMutable_(object.id);
        if (!service_ || !current || current->paintId == 0) {
            return;
        }
        if (SameShape(object, *current) && UsesPatches(object.look) == UsesPatches(current->look)) {
            // A colour, opacity or texture change: no new cut.
            shownStyle_.insert(object.id);
            Restyle_(object, AreaOf(current->id), current->paintId, current->patchPaintIds, current->drawOrder,
                     current->hidden, false);
            return;
        }
        const Area area = BuildArea(object);
        if (IsEmpty(area)) {
            return;
        }
        shownShape_.insert(object.id);
        ShowParts_(current->paintId, current->patchPaintIds, object, area, BuildPatches(object, area, kPreviewPatchCells),
                   BuildUvMap(object), current->drawOrder, current->hidden, false);
    }

    Bounds GroundPaintDocument::SelectionBounds() const {
        Area all;
        for (const uint32_t id : selection_) {
            const Area& area = AreaOf(id);
            all.insert(all.end(), area.begin(), area.end());
        }
        return Ground::BoundsOf(all);
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
            Built& built = built_[object.id];
            built.area = BuildArea(object);
            built.bounds = Ground::BoundsOf(built.area);
            built.patches = BuildPatches(object, built.area, kPatchCells);
        }
        return true;
    }

    void GroundPaintDocument::Restore() {
        TerrainPaintStyle existing{};
        const auto exists = [&](const uint32_t id) {
            return service_ && id != 0 && service_->GetPaintStyle(TerrainPaintId{id}, &existing, sizeof(existing));
        };
        for (PaintObject& object : objects_) {
            // The service saved the paints with the city: keep them, they are the same ones.
            if (exists(object.paintId) && std::ranges::all_of(object.patchPaintIds, exists) &&
                object.patchPaintIds.size() == built_[object.id].patches.size()) {
                continue;
            }
            // Some are missing: make them all again, in order.
            for (const uint32_t id : object.patchPaintIds) {
                if (exists(id)) {
                    service_->RemovePaint(TerrainPaintId{id});
                }
            }
            if (exists(object.paintId)) {
                service_->RemovePaint(TerrainPaintId{object.paintId});
            }
            object.paintId = 0;
            object.patchPaintIds.clear();
            Sync_(object);
        }
    }
}
