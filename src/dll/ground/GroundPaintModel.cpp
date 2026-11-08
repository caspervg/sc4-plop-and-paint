#include "GroundPaintModel.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Ground {
    namespace {
        void ScaleMarking(MarkingSpec& marking, const float scale) {
            for (float* length : {&marking.lineWidth, &marking.stallWidth, &marking.stallDepth, &marking.aisleWidth,
                                  &marking.stripeWidth, &marking.stripeGap, &marking.stripeLength,
                                  &marking.lineSpacing, &marking.dashLength, &marking.gapLength,
                                  &marking.hatchSpacing, &marking.arrowWidth, &marking.textHeight}) {
                *length *= scale;
            }
        }
    }

    namespace {
        PatternFrame FrameOf(const ShapeSpec& shape) {
            return {.origin = shape.patternOrigin, .turnRadians = shape.patternTurn};
        }
    }

    Area BuildArea(const PaintObject& object) {
        const ShapeSpec& shape = object.shape;
        Area area;
        switch (shape.kind) {
        case ShapeKind::Area:
            area = AreaFromOutline(PathThrough(shape.points, shape.pathStyle, true), shape.cornerRadius);
            break;
        case ShapeKind::Line:
            // The soft edge fades inwards, so the line grows by it on both sides: the solid core
            // keeps the chosen width.
            area = StrokePath(PathThrough(shape.points, shape.pathStyle, false),
                              shape.width + 2.0f * object.look.featherMeters,
                              shape.ends,
                              {.dashLength = shape.dashLength, .gapLength = shape.gapLength});
            break;
        case ShapeKind::Brush:
            area = BrushStroke(shape.points, shape.brushRadius);
            if (!shape.outline.empty()) {
                // Strokes joined before this one.
                area = area.empty() ? shape.outline : Unite(shape.outline, area);
            }
            break;
        case ShapeKind::Marking:
            area = BuildMarking(shape.marking, shape.points, shape.pathStyle);
            break;
        case ShapeKind::Merged:
            area = shape.outline;
            break;
        }

        if (shape.roughness > 0.0f && shape.kind != ShapeKind::Marking && !area.empty()) {
            area = Roughened(area, shape.roughness, FrameOf(shape), shape.wearSeed ^ 0x5EEDu);
        }

        if (!object.erased.empty() && !area.empty()) {
            area = Subtract(area, object.erased);
        }
        return area;
    }

    std::vector<UvVertex> BuildUvMap(const PaintObject& object) {
        const ShapeSpec& shape = object.shape;
        if (!object.look.followPath || object.look.textures.empty() || shape.kind != ShapeKind::Line) {
            return {};
        }
        // The texture spans the painted width, soft edge and rough edge included, as BuildArea
        // makes it.
        return PathUvStrip(PathThrough(shape.points, shape.pathStyle, false),
                           0.5f * shape.width + object.look.featherMeters + std::max(0.0f, shape.roughness),
                           std::max(0.1f, object.look.textureSizeMeters));
    }

    bool UsesPatches(const Look& look) {
        return look.mix == TextureMix::Patches && look.textures.size() >= 2;
    }

    std::vector<Area> BuildPatches(const PaintObject& object, const Area& area, const size_t maxCells) {
        const Look& look = object.look;
        std::vector<Area> patches;
        if (!UsesPatches(look) || area.empty()) {
            return patches;
        }
        // Each layer covers the share of what lies under it that makes every texture show by its
        // weight: the top one its own share, the next its share of the rest, and so on.
        float below = std::max(0.0f, look.textures.front().weight);
        for (size_t i = 1; i < look.textures.size(); ++i) {
            const float weight = std::max(0.0f, look.textures[i].weight);
            below += weight;
            const float coverage = below > 0.0f ? weight / below : 0.0f;
            patches.push_back(PatchesOf(area, std::max(1.0f, look.patchSizeMeters), coverage, FrameOf(object.shape),
                                        look.variationSeed * 31u + static_cast<uint32_t>(i) * 7919u, maxCells));
        }
        return patches;
    }

    bool SameShape(const PaintObject& a, const PaintObject& b) {
        if (!(a.shape == b.shape) || a.erased != b.erased) {
            return false;
        }
        if (a.shape.kind == ShapeKind::Line &&
            (a.look.featherMeters != b.look.featherMeters || a.look.followPath != b.look.followPath ||
             a.look.textures.empty() != b.look.textures.empty() || a.look.textureSizeMeters != b.look.textureSizeMeters)) {
            return false;
        }
        if (UsesPatches(a.look) || UsesPatches(b.look)) {
            return a.look.mix == b.look.mix && a.look.textures == b.look.textures &&
                   a.look.patchSizeMeters == b.look.patchSizeMeters && a.look.variationSeed == b.look.variationSeed;
        }
        return true;
    }

    void TransformObject(PaintObject& object, const Placement& placement) {
        const auto move = [&](const Area& area) {
            return Transformed(area, placement.pivot, placement.rotationRadians, placement.scale, placement.offset);
        };
        ShapeSpec& shape = object.shape;
        if (!shape.points.empty()) {
            shape.points = move(Area{shape.points}).front();
        }
        shape.outline = move(shape.outline);
        object.erased = move(object.erased);
        shape.patternOrigin = move(Area{{shape.patternOrigin}}).front().front();
        shape.patternTurn += placement.rotationRadians;

        const float scale = placement.scale;
        shape.cornerRadius *= scale;
        shape.width *= scale;
        shape.brushRadius *= scale;
        shape.dashLength *= scale;
        shape.gapLength *= scale;
        shape.roughness *= scale;
        ScaleMarking(shape.marking, scale);
        object.look.featherMeters *= scale;
        object.look.patchSizeMeters *= scale;
        object.look.patchBlendMeters *= scale;
    }

    void MirrorObject(PaintObject& object, const Vec2 pivot, const float axisRadians) {
        ShapeSpec& shape = object.shape;
        for (Vec2& p : shape.points) {
            p = MirroredPoint(p, pivot, axisRadians);
        }
        shape.outline = Mirrored(shape.outline, pivot, axisRadians);
        object.erased = Mirrored(object.erased, pivot, axisRadians);
        shape.patternOrigin = MirroredPoint(shape.patternOrigin, pivot, axisRadians);
        shape.patternTurn = 2.0f * axisRadians - shape.patternTurn;

        if (shape.kind != ShapeKind::Marking) {
            return;
        }
        MarkingSpec& marking = shape.marking;
        switch (marking.kind) {
        case MarkingKind::ParkingRow:
        case MarkingKind::CarPark:
            // The bays are on the left of the clicks: the other way round puts them
            // on the mirrored side.
            std::ranges::reverse(shape.points);
            break;
        case MarkingKind::Hatching:
            marking.hatchAngleDegrees = -marking.hatchAngleDegrees;
            break;
        case MarkingKind::Arrow: {
            constexpr std::pair<ArrowKind, ArrowKind> kSwaps[] = {
                {ArrowKind::Left, ArrowKind::Right},
                {ArrowKind::StraightLeft, ArrowKind::StraightRight},
            };
            for (const auto& [a, b] : kSwaps) {
                if (marking.arrow == a) {
                    marking.arrow = b;
                }
                else if (marking.arrow == b) {
                    marking.arrow = a;
                }
            }
            break;
        }
        default:
            break;
        }
    }

    PlantingOutline PlantingOutlineOf(const PaintObject& object, const Area& area, const bool along,
                                      const size_t maxPoints) {
        PlantingOutline outline;
        if (along && object.shape.kind == ShapeKind::Line) {
            outline.points = PathThrough(object.shape.points, object.shape.pathStyle, false);
            outline.closed = false;
        }
        else {
            // The biggest ring is the outside of the biggest part; holes and smaller parts are
            // left to the mask of the fill.
            double biggest = 0.0;
            for (const Ring& ring : area) {
                double twice = 0.0;
                for (size_t i = 0; i < ring.size(); ++i) {
                    const Vec2 a = ring[i];
                    const Vec2 b = ring[(i + 1) % ring.size()];
                    twice += static_cast<double>(a.x) * b.z - static_cast<double>(b.x) * a.z;
                }
                if (std::abs(twice) > biggest) {
                    biggest = std::abs(twice);
                    outline.points = ring;
                }
            }
            outline.closed = true;
        }
        // Curves and rough edges have many points: thin them until the sketch is small enough to
        // show and fill quickly.
        for (float tolerance = 0.05f; outline.points.size() > std::max<size_t>(3, maxPoints) && tolerance < 64.0f;
             tolerance *= 2.0f) {
            outline.points = SimplifyPath(outline.points, tolerance, outline.closed);
        }
        return outline;
    }

    bool HasEditablePoints(const PaintObject& object) {
        switch (object.shape.kind) {
        case ShapeKind::Area:
        case ShapeKind::Line:
        case ShapeKind::Marking:
            return true;
        case ShapeKind::Brush:
        case ShapeKind::Merged:
            return false;
        }
        return false;
    }

    size_t MinimumPoints(const PaintObject& object) {
        switch (object.shape.kind) {
        case ShapeKind::Area:
            return 3;
        case ShapeKind::Line:
            return 2;
        case ShapeKind::Marking:
            return PointsFor(object.shape.marking.kind).minPoints;
        case ShapeKind::Brush:
        case ShapeKind::Merged:
            return 1;
        }
        return 1;
    }

    bool PointsClosed(const PaintObject& object) {
        switch (object.shape.kind) {
        case ShapeKind::Area:
            return true;
        case ShapeKind::Marking:
            return PointsFor(object.shape.marking.kind).closed;
        default:
            return false;
        }
    }

    const char* ShapeKindName(const ShapeKind kind) {
        switch (kind) {
        case ShapeKind::Area:
            return "Area";
        case ShapeKind::Line:
            return "Line";
        case ShapeKind::Brush:
            return "Brush";
        case ShapeKind::Marking:
            return "Marking";
        case ShapeKind::Merged:
            return "Merged";
        }
        return "Shape";
    }
}
