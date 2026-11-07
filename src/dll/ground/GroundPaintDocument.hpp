#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "GroundPaintModel.hpp"

class cIGZTerrainPaintService;

namespace Ground {
    // The ground paint of the current city: the objects, their order, the selection, and undo and
    // redo. Every change is mirrored to SC4RenderServices' terrain paint, which draws it and keeps
    // it in the city save; the objects themselves go into Plop and Paint's own save record, so they
    // stay editable.
    class GroundPaintDocument {
    public:
        // whiteTextureInstance: a plain white terrain decal texture, for paint in a colour.
        explicit GroundPaintDocument(uint32_t whiteTextureInstance);
        ~GroundPaintDocument();
        GroundPaintDocument(const GroundPaintDocument&) = delete;
        GroundPaintDocument& operator=(const GroundPaintDocument&) = delete;

        // Not owned. Changes made while there is no service are kept and drawn once there is one.
        void SetPaintService(cIGZTerrainPaintService* service);
        [[nodiscard]] bool CanDraw() const { return service_ != nullptr; }

        // Forgets everything, for a new city. The city's terrain paints are the service's to drop.
        void Reset();

        // Bottom to top.
        [[nodiscard]] const std::vector<PaintObject>& Objects() const { return objects_; }
        [[nodiscard]] const PaintObject* Find(uint32_t id) const;
        [[nodiscard]] const Area& AreaOf(uint32_t id) const;
        [[nodiscard]] Bounds BoundsOf(uint32_t id) const;
        // Goes up with every change to what is painted, so others can tell their caches are stale.
        [[nodiscard]] uint64_t Revision() const { return revision_; }

        // Each of these is one undo step.
        uint32_t Add(PaintObject object);  // on top; 0 when it covers nothing
        void Update(std::vector<PaintObject> changed);
        void Remove(std::span<const uint32_t> ids);
        void Erase(const Area& eraser);
        void MoveUp(uint32_t id);
        void MoveDown(uint32_t id);
        void MoveToTop(uint32_t id);
        void MoveToBottom(uint32_t id);
        // Copies, on top and moved by offset; they become the selection.
        void Duplicate(std::span<const uint32_t> ids, Vec2 offset);
        // Makes the unlocked ones into one object, painted like the topmost; 0 when fewer than two.
        uint32_t Merge(std::span<const uint32_t> ids);
        // A group is selected as one.
        void Group(std::span<const uint32_t> ids);
        void Ungroup(std::span<const uint32_t> ids);
        // Moves, turns and scales the unlocked ones.
        void Transform(std::span<const uint32_t> ids, const Placement& placement);
        // A new object along the edge of these, painted like the topmost of them, on top and
        // selected; 0 when there is no edge.
        uint32_t AddBorder(std::span<const uint32_t> ids, float width, BorderSide side);
        // Copies in a grid, columns by rows with the originals in the first place, each column
        // and row moved one step further. All of them become the selection. At most kMaxRepeats
        // copies.
        static constexpr size_t kMaxRepeats = 400;
        void Repeat(std::span<const uint32_t> ids, int columns, int rows, Vec2 columnStep, Vec2 rowStep);
        // Mirrors the unlocked ones in the line through pivot at axis radians, or copies them
        // mirrored (the copies become the selection).
        void Mirror(std::span<const uint32_t> ids, Vec2 pivot, float axisRadians, bool copy);

        bool Undo();
        bool Redo();
        [[nodiscard]] bool CanUndo() const { return !undo_.empty(); }
        [[nodiscard]] bool CanRedo() const { return !redo_.empty(); }

        // The topmost visible object at a point.
        [[nodiscard]] std::optional<uint32_t> HitTest(Vec2 point) const;

        [[nodiscard]] const std::vector<uint32_t>& Selection() const { return selection_; }
        [[nodiscard]] bool IsSelected(uint32_t id) const;
        // With its whole group.
        void Select(uint32_t id, bool addToSelection);
        void ClearSelection();

        // Shows an object that is still being drawn; nullptr hides it. Not saved, not undone.
        void ShowPreview(const PaintObject* object);
        // Shows the selection moved while it is dragged; a zero offset shows it where it is.
        void ShowSelectionMoved(Vec2 offset);
        void ShowSelectionTransformed(const Placement& placement);
        // Shows an object as it would be after a change that is still being made, such as a
        // point being dragged or a colour being picked. A change of look alone only restyles the
        // paint; a change of shape cuts it again. ShowSelectionMoved({}) shows the selection as it
        // is again.
        void ShowChanged(const PaintObject& object);
        [[nodiscard]] Bounds SelectionBounds() const;

        // The save record's payload, and back.
        [[nodiscard]] std::vector<char> Save() const;
        bool Load(std::span<const char> bytes);
        // After a load, once the city and its terrain paints are there: makes the terrain paint of
        // every object that has none.
        void Restore();

    private:
        struct Step {
            std::vector<PaintObject> before;
            std::vector<PaintObject> after;
        };

        // What an object covers, worked out once per change.
        struct Built {
            Area area;
            Bounds bounds;
            std::vector<Area> patches;
        };

        void Commit_(std::vector<PaintObject> before, std::vector<PaintObject> after);
        void Apply_(const std::vector<PaintObject>& from, const std::vector<PaintObject>& to);
        void Upsert_(PaintObject object);
        void RemoveObject_(uint32_t id);
        void Sync_(PaintObject& object);
        // Makes or updates the terrain paints of an area and its patches: one id per part. All
        // are made again when one has to be, so the patches stay on top of the area.
        void ShowParts_(uint32_t& paintId, std::vector<uint32_t>& patchIds, const PaintObject& object,
                        const Area& area, const std::vector<Area>& patches, const std::vector<UvVertex>& uvMap,
                        int32_t drawOrder, bool hidden, bool transient);
        // Restyles the terrain paints of an object without touching their shapes.
        void Restyle_(const PaintObject& object, const Area& area, uint32_t paintId,
                      const std::vector<uint32_t>& patchIds, int32_t drawOrder, bool hidden, bool transient);
        void RemovePaint_(uint32_t& paintId);
        void RemovePaints_(uint32_t& paintId, std::vector<uint32_t>& patchIds);
        void SortByOrder_();
        void Reorder_(uint32_t id, int steps);
        [[nodiscard]] PaintObject* FindMutable_(uint32_t id);
        [[nodiscard]] int32_t TopOrder_() const;

        uint32_t whiteTextureInstance_;
        cIGZTerrainPaintService* service_ = nullptr;
        std::vector<PaintObject> objects_{};
        std::unordered_map<uint32_t, Built> built_{};
        // Objects whose terrain paints show a change that is still being made: a moved or
        // reshaped cut, or only a new style. ShowSelectionMoved({}) puts back just these.
        std::unordered_set<uint32_t> shownShape_{};
        std::unordered_set<uint32_t> shownStyle_{};
        std::vector<uint32_t> selection_{};
        std::vector<Step> undo_{};
        std::vector<Step> redo_{};
        uint32_t nextId_ = 1;
        uint32_t previewPaintId_ = 0;
        std::vector<uint32_t> previewPatchIds_{};
        // What the preview shows, to tell a new look from a new shape.
        std::optional<PaintObject> previewObject_{};
        Area previewArea_{};
        uint64_t revision_ = 1;
    };
}
