// Checks for the ground paint document against a fake terrain paint service: run
// GroundPaintDocumentTests.exe, it prints "ok" or aborts.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

#include "ground/GroundPaintDocument.hpp"
#include "public/cIGZTerrainPaintService.h"

using namespace Ground;

namespace {
    void Check(const bool condition, const char* what, const int line) {
        if (!condition) {
            std::fprintf(stderr, "FAILED line %d: %s\n", line, what);
            std::abort();
        }
    }
#define CHECK(x) Check((x), #x, __LINE__)

    class FakePaintService final : public cIGZTerrainPaintService {
    public:
        struct Paint {
            TerrainPaintStyle style;
            uint32_t ringCount = 0;
            uint32_t pointCount = 0;
        };

        bool QueryInterface(uint32_t, void**) override { return false; }
        uint32_t AddRef() override { return 1; }
        uint32_t Release() override { return 1; }
        uint32_t GetStyleSize() const override { return sizeof(TerrainPaintStyle); }

        bool CreatePaint(const TerrainPaintShape& shape, const TerrainPaintStyle* style, uint32_t,
                         TerrainPaintId* outId) override {
            outId->value = nextId++;
            paints[outId->value] = Paint{*style, shape.ringCount, Points(shape)};
            ++creates;
            return true;
        }
        bool RemovePaint(const TerrainPaintId id) override { return paints.erase(id.value) > 0; }
        bool SetPaintShape(const TerrainPaintId id, const TerrainPaintShape& shape) override {
            const auto it = paints.find(id.value);
            if (it == paints.end()) {
                return false;
            }
            it->second.ringCount = shape.ringCount;
            it->second.pointCount = Points(shape);
            ++shapeSets;
            return true;
        }
        bool SetPaintStyle(const TerrainPaintId id, const TerrainPaintStyle* style, uint32_t) override {
            const auto it = paints.find(id.value);
            if (it == paints.end()) {
                return false;
            }
            it->second.style = *style;
            return true;
        }
        bool GetPaintStyle(const TerrainPaintId id, TerrainPaintStyle* out, uint32_t) const override {
            const auto it = paints.find(id.value);
            if (it == paints.end()) {
                return false;
            }
            *out = it->second.style;
            return true;
        }
        uint32_t GetPaintCount() const override { return static_cast<uint32_t>(paints.size()); }

        std::map<uint32_t, Paint> paints;
        uint32_t nextId = 100;
        int creates = 0;
        int shapeSets = 0;

    private:
        static uint32_t Points(const TerrainPaintShape& shape) {
            uint32_t total = 0;
            for (uint32_t i = 0; i < shape.ringCount; ++i) {
                total += shape.ringSizes[i];
            }
            return total;
        }
    };

    constexpr uint32_t kWhite = 0x5A4E0001;

    PaintObject Square(const float x0, const float z0, const float size) {
        PaintObject object;
        object.shape.kind = ShapeKind::Area;
        object.shape.points = {{x0, z0}, {x0 + size, z0}, {x0 + size, z0 + size}, {x0, z0 + size}};
        return object;
    }
}

int main() {
    FakePaintService service;
    GroundPaintDocument doc(kWhite);
    doc.SetPaintService(&service);

    // Add: one terrain paint, a plain colour uses the white texture, stacked in order.
    const uint32_t a = doc.Add(Square(0, 0, 20));
    PaintObject textured = Square(10, 10, 20);
    textured.look.textures = {{0x1234, 1.0f}, {0x5678, 3.0f}};
    const uint32_t b = doc.Add(textured);
    CHECK(a != 0 && b != 0 && a != b);
    CHECK(service.paints.size() == 2);
    const auto& paintA = service.paints.at(doc.Find(a)->paintId);
    CHECK(paintA.style.textureCount == 1 && paintA.style.textures[0].key.instance == kWhite);
    const auto& paintB = service.paints.at(doc.Find(b)->paintId);
    CHECK(paintB.style.textureCount == 2 && paintB.style.textures[1].weight == 3.0f);
    CHECK(paintB.style.drawOrder > paintA.style.drawOrder);
    CHECK(doc.Add(PaintObject{}) == 0);  // nothing to paint

    // Hit test finds the topmost; hidden objects are skipped.
    CHECK(doc.HitTest({15, 15}) == b);
    CHECK(doc.HitTest({5, 5}) == a);
    CHECK(!doc.HitTest({50, 50}));

    // Update, undo, redo.
    PaintObject changed = *doc.Find(a);
    changed.look.color = {1.0f, 0.0f, 0.0f};
    changed.hidden = true;
    doc.Update({changed});
    CHECK(!service.paints.at(doc.Find(a)->paintId).style.enabled);
    CHECK(doc.HitTest({5, 5}) != a);
    CHECK(doc.Undo());
    CHECK(service.paints.at(doc.Find(a)->paintId).style.enabled);
    CHECK(doc.Redo());
    CHECK(doc.Find(a)->hidden);
    CHECK(doc.Undo());

    // Remove and undo bring the paint back as a new terrain paint.
    const uint32_t removedPaint = doc.Find(b)->paintId;
    const uint32_t ids[] = {b};
    doc.Remove(ids);
    CHECK(!doc.Find(b) && !service.paints.contains(removedPaint));
    CHECK(doc.Undo());
    CHECK(doc.Find(b) && service.paints.contains(doc.Find(b)->paintId));

    // Reordering moves the paint below the other.
    doc.MoveToBottom(b);
    CHECK(doc.Objects().front().id == b);
    CHECK(service.paints.at(doc.Find(b)->paintId).style.drawOrder <
          service.paints.at(doc.Find(a)->paintId).style.drawOrder);
    CHECK(doc.HitTest({15, 15}) == a);
    CHECK(doc.Undo());
    CHECK(doc.Objects().back().id == b);

    // Erase cuts a hole; erasing everything removes the object; locked objects are left alone.
    doc.Erase(AreaFromOutline(std::vector<Vec2>{{2, 2}, {6, 2}, {6, 6}, {2, 6}}));
    CHECK(!doc.Find(a)->erased.empty());
    CHECK(doc.HitTest({4, 4}) != a && doc.HitTest({1, 1}) == a);
    CHECK(service.paints.at(doc.Find(a)->paintId).ringCount == 2);
    PaintObject locked = *doc.Find(b);
    locked.locked = true;
    doc.Update({locked});
    doc.Erase(AreaFromOutline(std::vector<Vec2>{{-5, -5}, {50, -5}, {50, 50}, {-5, 50}}));
    CHECK(!doc.Find(a) && doc.Find(b));
    CHECK(doc.Undo() && doc.Find(a));

    // Preview: a transient paint on top, gone when hidden.
    const size_t before = service.paints.size();
    PaintObject drawing = Square(100, 100, 5);
    doc.ShowPreview(&drawing);
    CHECK(service.paints.size() == before + 1);
    bool sawTransient = false;
    for (const auto& [id, paint] : service.paints) {
        sawTransient = sawTransient || (paint.style.flags & kTerrainPaintFlagTransient) != 0;
    }
    CHECK(sawTransient);
    doc.ShowPreview(nullptr);
    CHECK(service.paints.size() == before);

    // Save and load: objects come back; Restore keeps paints the service still has and makes the
    // missing ones again.
    const std::vector<char> saved = doc.Save();
    const uint32_t keptPaint = doc.Find(a)->paintId;
    service.paints.erase(doc.Find(b)->paintId);
    GroundPaintDocument loaded(kWhite);
    loaded.SetPaintService(&service);
    CHECK(loaded.Load(saved));
    CHECK(loaded.Objects().size() == 2 && loaded.Find(b)->locked && !loaded.Find(a)->erased.empty());
    const int createsBefore = service.creates;
    loaded.Restore();
    CHECK(loaded.Find(a)->paintId == keptPaint);
    CHECK(service.creates == createsBefore + 1 && service.paints.contains(loaded.Find(b)->paintId));
    CHECK(loaded.Add(Square(200, 200, 4)) > b);  // ids continue after the loaded ones
    CHECK(!loaded.Load(std::vector<char>{'x', 'y'}));
    {
        // A save from a newer version, with a kind of shape this one does not know, cannot be read.
        std::vector<char> newer = saved;
        constexpr char kKnown[] = {'A', 'r', 'e', 'a'};
        int renamed = 0;
        for (auto it = newer.begin(); (it = std::search(it, newer.end(), kKnown, kKnown + 4)) != newer.end(); ++renamed) {
            it[3] = 'x';
        }
        CHECK(renamed > 0);
        GroundPaintDocument older(kWhite);
        CHECK(!older.Load(newer) && older.Objects().empty());
    }

    // Duplicate: copies on top, moved, selected.
    GroundPaintDocument edits(kWhite);
    edits.SetPaintService(&service);
    const uint32_t p = edits.Add(Square(0, 0, 10));
    const uint32_t q = edits.Add(Square(20, 0, 10));
    const uint32_t both[] = {p, q};
    edits.Duplicate(both, {0, 50});
    CHECK(edits.Objects().size() == 4 && edits.Selection().size() == 2);
    CHECK(edits.HitTest({5, 55}) == edits.Selection()[0]);
    CHECK(edits.Undo() && edits.Objects().size() == 2);

    // Group: picking one selects both; ungroup splits them again.
    edits.Group(both);
    edits.Select(p, false);
    CHECK(edits.Selection().size() == 2);
    edits.Ungroup(both);
    edits.Select(p, false);
    CHECK(edits.Selection().size() == 1);

    // Transform: turned a quarter about the first square's centre, scaled by two.
    const uint32_t justP[] = {p};
    edits.Transform(justP, {.pivot = {5, 5}, .rotationRadians = 1.5707964f, .scale = 2.0f});
    CHECK(std::abs(AreaOf(edits.AreaOf(p)) - 400.0) < 0.5);
    CHECK(edits.HitTest({-4, -4}) == p);
    CHECK(edits.Undo());

    // Merge: one object where both were, the topmost's terrain paint kept.
    const uint32_t topPaint = edits.Find(q)->paintId;
    const uint32_t merged = edits.Merge(both);
    CHECK(merged == q && edits.Objects().size() == 1 && edits.Find(q)->shape.kind == ShapeKind::Merged);
    CHECK(edits.Find(q)->paintId == topPaint && service.paints.at(topPaint).ringCount == 2);
    CHECK(edits.HitTest({5, 5}) == q && edits.HitTest({25, 5}) == q);
    CHECK(edits.Undo() && edits.Objects().size() == 2);

    // A marking with wear survives a save and load as it was.
    PaintObject marking;
    marking.shape.kind = ShapeKind::Marking;
    marking.shape.marking.kind = MarkingKind::Zebra;
    marking.shape.points = {{0, 100}, {10, 100}};
    marking.shape.wear = 0.3f;
    const uint32_t m = edits.Add(marking);
    CHECK(m != 0 && edits.Find(m)->shape.wearSeed != 0);
    // Wear is SC4RenderServices' to cut: it goes in the style, the area stays whole.
    const TerrainPaintStyle& wornStyle = service.paints.at(edits.Find(m)->paintId).style;
    CHECK(wornStyle.wear == 0.3f && wornStyle.wearSeed == edits.Find(m)->shape.wearSeed && wornStyle.wearPatchMeters > 0.0f);
    PaintObject unworn = *edits.Find(m);
    unworn.shape.wear = 0.0f;
    CHECK(std::abs(AreaOf(BuildArea(unworn)) - AreaOf(edits.AreaOf(m))) < 1.0e-6);
    GroundPaintDocument reloaded(kWhite);
    CHECK(reloaded.Load(edits.Save()));
    CHECK(reloaded.Find(m)->shape.marking.kind == MarkingKind::Zebra);
    CHECK(std::abs(AreaOf(reloaded.AreaOf(m)) - AreaOf(edits.AreaOf(m))) < 1.0e-6);

    // A change of look alone restyles the paint without cutting it again; a change of shape does.
    {
        GroundPaintDocument looks(kWhite);
        looks.SetPaintService(&service);
        const uint32_t id = looks.Add(Square(0, 0, 10));
        PaintObject recoloured = *looks.Find(id);
        recoloured.look.color = {0.2f, 0.4f, 0.6f};
        recoloured.look.textureOffset = {3.0f, -2.0f};
        recoloured.look.depthOffset = 5;
        int shapeSets = service.shapeSets;
        looks.Update({recoloured});
        CHECK(service.shapeSets == shapeSets);
        const TerrainPaintStyle& style = service.paints.at(looks.Find(id)->paintId).style;
        CHECK(style.color.fY == 0.4f && style.textureOffsetX == 3.0f && style.textureOffsetZ == -2.0f &&
              style.depthOffset == 5);
        // Shown while a control is held, then put back.
        recoloured.look.opacity = 0.5f;
        looks.ShowChanged(recoloured);
        CHECK(service.shapeSets == shapeSets && service.paints.at(looks.Find(id)->paintId).style.opacity == 0.5f);
        looks.Select(id, false);
        looks.ShowSelectionMoved({});
        CHECK(service.paints.at(looks.Find(id)->paintId).style.opacity == 1.0f);
        // Order changes restyle too.
        looks.Add(Square(50, 0, 10));
        shapeSets = service.shapeSets;
        looks.MoveToTop(id);
        CHECK(service.shapeSets == shapeSets);
        CHECK(service.paints.at(looks.Find(id)->paintId).style.drawOrder == looks.Objects().back().drawOrder);
        // Dragged, then let go: the move cuts it once, putting the selection back cuts nothing more.
        looks.Select(id, false);
        looks.ShowSelectionTransformed({.offset = {5.0f, 0.0f}});
        CHECK(service.shapeSets == shapeSets + 1);
        const uint32_t dragged[] = {id};
        looks.Transform(dragged, {.offset = {5.0f, 0.0f}});
        const int afterMove = service.shapeSets;
        looks.ShowSelectionMoved({});
        CHECK(service.shapeSets == afterMove);
        shapeSets = afterMove;
        PaintObject rough = *looks.Find(id);
        rough.shape.roughness = 0.5f;
        looks.Update({rough});
        CHECK(service.shapeSets > shapeSets);
        CHECK(std::abs(AreaOf(looks.AreaOf(id)) - 100.0) < 15.0);
    }

    // Texture patches: one terrain paint per texture, the later ones patches on the first.
    {
        GroundPaintDocument mixed(kWhite);
        mixed.SetPaintService(&service);
        PaintObject meadow = Square(0, 0, 64);
        meadow.look.textures = {{0x1111, 1.0f}, {0x2222, 1.0f}, {0x3333, 1.0f}};
        meadow.look.mix = TextureMix::Patches;
        meadow.look.variationSeed = 7;
        const size_t paintsBefore = service.paints.size();
        const uint32_t id = mixed.Add(meadow);
        const PaintObject& added = *mixed.Find(id);
        CHECK(added.patchPaintIds.size() == 2 && service.paints.size() == paintsBefore + 3);
        CHECK(service.paints.at(added.paintId).style.textureCount == 1 &&
              service.paints.at(added.paintId).style.textures[0].key.instance == 0x1111);
        const TerrainPaintStyle& patch = service.paints.at(added.patchPaintIds[1]).style;
        CHECK(patch.textures[0].key.instance == 0x3333 && patch.featherMeters == meadow.look.patchBlendMeters);
        CHECK(service.paints.at(added.patchPaintIds[0]).pointCount > 3);
        // Moved: the patches go along; removed: all paints go.
        const uint32_t ids[] = {id};
        mixed.Transform(ids, {.offset = {100.0f, 0.0f}});
        CHECK(mixed.Find(id)->patchPaintIds.size() == 2);
        CHECK(mixed.Find(id)->shape.patternOrigin.x == 100.0f);
        // Saved paints are kept by Restore.
        const std::vector<char> bytes = mixed.Save();
        GroundPaintDocument again(kWhite);
        again.SetPaintService(&service);
        CHECK(again.Load(bytes));
        const int creates = service.creates;
        again.Restore();
        CHECK(service.creates == creates);
        mixed.Remove(ids);
        CHECK(service.paints.size() == paintsBefore);
        CHECK(mixed.Undo() && service.paints.size() == paintsBefore + 3);
        // Back to one texture: the patch paints go.
        PaintObject plain = *mixed.Find(id);
        plain.look.textures.resize(1);
        mixed.Update({plain});
        CHECK(mixed.Find(id)->patchPaintIds.empty() && service.paints.size() == paintsBefore + 1);
    }

    // Borders, repeats and mirrors.
    {
        GroundPaintDocument tools(kWhite);
        tools.SetPaintService(&service);
        const uint32_t square = tools.Add(Square(0, 0, 10));
        const uint32_t ids[] = {square};
        const uint32_t border = tools.AddBorder(ids, 1.0f, BorderSide::Outside);
        CHECK(border != 0 && tools.Selection() == std::vector<uint32_t>{border});
        CHECK(std::abs(AreaOf(tools.AreaOf(border)) - 44.0) < 0.05);
        CHECK(tools.HitTest({-0.5f, 5}) == border && tools.HitTest({5, 5}) == square);
        CHECK(tools.Undo() && !tools.Find(border));

        tools.Repeat(ids, 3, 2, {20, 0}, {0, 20});
        CHECK(tools.Objects().size() == 6 && tools.Selection().size() == 6);
        CHECK(tools.HitTest({45, 25}) && tools.HitTest({45, 25}) != square);
        CHECK(tools.Undo() && tools.Objects().size() == 1);

        PaintObject l;
        l.shape.kind = ShapeKind::Area;
        l.shape.points = {{0, 0}, {4, 0}, {4, 1}, {1, 1}, {1, 3}, {0, 3}};
        const uint32_t shapeL = tools.Add(l);
        const uint32_t justL[] = {shapeL};
        tools.Mirror(justL, {10, 0}, 1.5707964f, true);  // in the line x = 10, as a copy
        CHECK(tools.Objects().size() == 3 && tools.Selection().size() == 1);
        const uint32_t copyL = tools.Selection().front();
        CHECK(tools.HitTest({19.5f, 2.5f}) == copyL && tools.HitTest({16.5f, 0.5f}) == copyL);
        CHECK(std::abs(AreaOf(tools.AreaOf(copyL)) - 6.0) < 0.01);
        tools.Mirror(justL, {10, 0}, 1.5707964f, false);  // in place
        CHECK(tools.HitTest({19.5f, 2.5f}) == shapeL || tools.HitTest({19.5f, 2.5f}) == copyL);
        CHECK(tools.BoundsOf(shapeL).min.x > 15.9f);

        // A mirrored parking row keeps its bays on the mirrored side.
        PaintObject row;
        row.shape.kind = ShapeKind::Marking;
        row.shape.marking.kind = MarkingKind::ParkingRow;
        row.shape.points = {{0, 50}, {20, 50}};
        const uint32_t parking = tools.Add(row);
        const Bounds before = tools.BoundsOf(parking);
        const uint32_t justRow[] = {parking};
        tools.Mirror(justRow, {0, 50}, 0.0f, false);  // in the line z = 50
        const Bounds after = tools.BoundsOf(parking);
        CHECK(std::abs((before.min.z - 50) + (after.max.z - 50)) < 0.05);
    }

    // Outlines for the prop and flora painters: the biggest part, or the path of a line, thinned.
    {
        PaintObject ring = Square(0, 0, 40);
        ring.erased = AreaFromOutline(std::vector<Vec2>{{10, 10}, {30, 10}, {30, 30}, {10, 30}});
        ring.shape.roughness = 1.0f;
        const Area area = BuildArea(ring);
        const PlantingOutline fill = PlantingOutlineOf(ring, area, false, 64);
        CHECK(fill.closed && fill.points.size() >= 3 && fill.points.size() <= 64);
        CHECK(std::abs(AreaOf(AreaFromOutline(fill.points)) - 1600.0) < 200.0);
        PaintObject line;
        line.shape.kind = ShapeKind::Line;
        line.shape.points = {{0, 0}, {50, 0}, {50, 50}};
        line.shape.pathStyle = PathStyle::Curved;
        const PlantingOutline along = PlantingOutlineOf(line, BuildArea(line), true, 32);
        CHECK(!along.closed && along.points.size() <= 32);
        CHECK(std::hypot(along.points.front().x, along.points.front().z) < 0.01f);
        CHECK(std::hypot(along.points.back().x - 50, along.points.back().z - 50) < 0.01f);
    }

    std::puts("ok");
    return 0;
}
