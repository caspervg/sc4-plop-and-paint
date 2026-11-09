#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "GroundGeometry.hpp"
#include "GroundMarkings.hpp"

// What the ground paint tools make: painted areas, lines, brush strokes and road markings. Each
// one keeps what it was drawn from (anchors, widths, corner radius) so it can be edited later, and
// is shown in the city as one terrain paint of SC4RenderServices. Plain data: it is saved with
// reflect-cpp.
namespace Ground {
    enum class ShapeKind : uint8_t {
        Area,
        Line,
        Brush,
        Marking,
        Merged,  // several objects made into one: only the outline is left
    };

    struct ShapeSpec {
        ShapeKind kind = ShapeKind::Area;
        // Area and line: the anchors the outline or path goes through. Brush: the brush centre
        // along the stroke.
        std::vector<Vec2> points{};
        PathStyle pathStyle = PathStyle::Straight;
        // Area and line drawn with the pen: the curve handle of each point (see PenPath). Empty
        // when there are no handles.
        std::vector<Vec2> handles{};
        // The handle on the side the curve arrives from. Empty: each mirrors its handle.
        std::vector<Vec2> inHandles{};
        // Area and line: the corner radius of each point; negative uses cornerRadius. Empty when
        // all corners use it.
        std::vector<float> pointRadii{};
        float cornerRadius = 0.0f;  // area
        float width = 0.5f;         // line
        LineEnds ends = LineEnds::Flat;
        float dashLength = 0.0f;    // line; 0 is solid
        float gapLength = 0.0f;
        float brushRadius = 2.0f;   // brush
        MarkingSpec marking{};      // marking, along the points
        // Merged: the outline. Brush: the strokes joined before the last one.
        Area outline{};
        // Worn paint, 0 to 1, and which blotches; any kind.
        float wear = 0.0f;
        uint32_t wearSeed = 0;
        // A rough, natural edge: how far it wanders, in metres. Not for markings.
        float roughness = 0.0f;
        // Where rough edges and texture patches are read from the noise; moves, turns and
        // scales with the object, so they keep their place on it.
        Vec2 patternOrigin{};
        float patternTurn = 0.0f;  // radians

        bool operator==(const ShapeSpec&) const = default;
    };

    struct TextureChoice {
        uint32_t instance = 0;  // a terrain decal texture, type 0x7AB50E44, group 0x0986135E
        float weight = 1.0f;

        bool operator==(const TextureChoice&) const = default;
    };

    // How several textures share the ground.
    enum class TextureMix : uint8_t {
        Squares,  // each 16 m terrain square takes one, by weight (the service does it)
        Patches,  // the first covers the shape; the others lie on it in soft-edged patches
    };

    // What a shape is painted with.
    struct Look {
        // Several textures are mixed by weight, as mix says. None paints a plain colour.
        std::vector<TextureChoice> textures{};
        std::array<float, 3> color{1.0f, 1.0f, 1.0f};
        float opacity = 1.0f;
        float featherMeters = 0.0f;
        float textureSizeMeters = 16.0f;
        float textureRotationDegrees = 0.0f;
        uint32_t variationSeed = 0;
        // Lines: the texture follows the line, repeating every textureSizeMeters along it and
        // spanning its width, instead of lying flat.
        bool followPath = false;
        // Where the flat texture starts, in metres: moves it to line it up with a kerb or a lot.
        std::array<float, 2> textureOffset{0.0f, 0.0f};
        // -1: the service default. Higher values draw over more of what lies at the same spot,
        // such as lot textures; 4 and up draws over the game's shadows.
        int32_t depthOffset = -1;
        TextureMix mix = TextureMix::Squares;
        float patchSizeMeters = 12.0f;
        float patchBlendMeters = 2.0f;  // the soft edge of the patches

        bool operator==(const Look&) const = default;
    };

    struct PaintObject {
        uint32_t id = 0;
        std::string name{};
        uint32_t group = 0;  // 0: not in a group
        bool hidden = false;
        bool locked = false;
        ShapeSpec shape{};
        // Rubbed out by the eraser, cut from the shape.
        Area erased{};
        Look look{};
        // Higher draws on top.
        int32_t drawOrder = 0;
        // The terrain paint that shows it, 0 while there is none.
        uint32_t paintId = 0;
        // With texture patches: the terrain paint of each patch texture, after the first.
        std::vector<uint32_t> patchPaintIds{};
    };

    // The area the object covers in the city. Wear is not in it: SC4RenderServices cuts it when it
    // draws the paint.
    [[nodiscard]] Area BuildArea(const PaintObject& object);
    // Whether the shape has handles or corners of its own, and so goes through PenPath.
    [[nodiscard]] bool UsesPenPath(const ShapeSpec& shape);
    // The path through the points of an area or line.
    [[nodiscard]] std::vector<Vec2> ShapePath(const ShapeSpec& shape, bool closed);
    // A handle that makes a smooth curve through the point, along the line between its neighbours.
    [[nodiscard]] Vec2 SmoothHandle(const std::vector<Vec2>& points, size_t index, bool closed);
    [[nodiscard]] Vec2 InHandleOf(const ShapeSpec& shape, size_t index);
    void SetPointHandles(ShapeSpec& shape, size_t index, Vec2 handle, Vec2 inHandle);
    // Add or remove a point, and what belongs to it.
    void InsertShapePoint(ShapeSpec& shape, size_t index, Vec2 point);
    void EraseShapePoint(ShapeSpec& shape, size_t index);
    // The UV map that makes its texture follow it; empty when the texture lies flat.
    [[nodiscard]] std::vector<UvVertex> BuildUvMap(const PaintObject& object);

    // Whether the object is painted in patches: one terrain paint per texture.
    [[nodiscard]] bool UsesPatches(const Look& look);
    // The patches of each texture after the first, on top of the area, in order. maxCells limits
    // the noise grid of each: lower for previews that are rebuilt as the mouse moves.
    [[nodiscard]] std::vector<Area> BuildPatches(const PaintObject& object, const Area& area, size_t maxCells);
    // How fine the noise of finished patches is.
    constexpr size_t kPatchCells = 60000;
    constexpr size_t kPreviewPatchCells = 6000;

    // Whether two versions of an object cover the same ground the same way, so a change from one
    // to the other only needs a new style.
    [[nodiscard]] bool SameShape(const PaintObject& a, const PaintObject& b);

    // A move, turn (radians) and scale about a pivot.
    struct Placement {
        Vec2 pivot{};
        float rotationRadians = 0.0f;
        float scale = 1.0f;
        Vec2 offset{};
    };

    // Erased parts move along.
    void TransformObject(PaintObject& object, const Placement& placement);
    // Mirrors the object in the line through pivot at angle (radians) to the x axis. Markings
    // keep reading the right way: bays and arrows swap sides, text is not written backwards.
    void MirrorObject(PaintObject& object, Vec2 pivot, float axisRadians);

    // A painted object as a sketch for the prop and flora painters: the path a line runs along
    // (along) or the outline of its biggest part, thinned to at most maxPoints points.
    struct PlantingOutline {
        std::vector<Vec2> points;
        bool closed = true;
    };
    [[nodiscard]] PlantingOutline PlantingOutlineOf(const PaintObject& object, const Area& area, bool along,
                                                    size_t maxPoints = 256);

    // Whether the object's points can be dragged one by one, and how many it needs.
    [[nodiscard]] bool HasEditablePoints(const PaintObject& object);
    [[nodiscard]] size_t MinimumPoints(const PaintObject& object);
    // Whether the points go round and back to the first.
    [[nodiscard]] bool PointsClosed(const PaintObject& object);

    [[nodiscard]] const char* ShapeKindName(ShapeKind kind);
}
