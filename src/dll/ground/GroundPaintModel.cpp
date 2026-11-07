#include "GroundPaintModel.hpp"

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
            break;
        case ShapeKind::Marking:
            area = BuildMarking(shape.marking, shape.points, shape.pathStyle);
            break;
        case ShapeKind::Merged:
            area = shape.outline;
            break;
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
        // The texture spans the painted width, soft edge included, as BuildArea makes it.
        return PathUvStrip(PathThrough(shape.points, shape.pathStyle, false),
                           0.5f * shape.width + object.look.featherMeters,
                           std::max(0.1f, object.look.textureSizeMeters));
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

        const float scale = placement.scale;
        shape.cornerRadius *= scale;
        shape.width *= scale;
        shape.brushRadius *= scale;
        shape.dashLength *= scale;
        shape.gapLength *= scale;
        ScaleMarking(shape.marking, scale);
        object.look.featherMeters *= scale;
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
