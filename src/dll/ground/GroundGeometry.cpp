#include "GroundGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <clipper2/clipper.h>
#include <tinysplinecxx.h>

namespace Ground {
    namespace {
        using namespace Clipper2Lib;

        // Millimetres: plenty on a 16 m grid, and keeps Clipper's integers small.
        constexpr int kPrecision = 3;
        // How far a rounded corner or line end may stray from a true circle.
        constexpr double kArcTolerance = 0.02;
        constexpr double kMiterLimit = 4.0;

        PathD ToPath(const std::span<const Vec2> points) {
            PathD path;
            path.reserve(points.size());
            for (const Vec2& p : points) {
                if (std::isfinite(p.x) && std::isfinite(p.z)) {
                    path.emplace_back(p.x, p.z);
                }
            }
            return path;
        }

        PathsD ToPaths(const Area& area) {
            PathsD paths;
            paths.reserve(area.size());
            for (const Ring& ring : area) {
                paths.push_back(ToPath(ring));
            }
            return paths;
        }

        Ring ToRing(const PathD& path) {
            Ring ring;
            ring.reserve(path.size());
            for (const PointD& p : path) {
                ring.push_back({static_cast<float>(p.x), static_cast<float>(p.y)});
            }
            return ring;
        }

        Area ToArea(const PathsD& paths) {
            Area area;
            area.reserve(paths.size());
            for (const PathD& path : paths) {
                if (path.size() >= 3) {
                    area.push_back(ToRing(path));
                }
            }
            return area;
        }

        // Clean, non-overlapping rings from anything even-odd.
        PathsD Normalized(const PathsD& paths) {
            return Union(paths, FillRule::EvenOdd, kPrecision);
        }

        PathsD Offset(const PathsD& paths, const double distance, const JoinType join, const EndType end) {
            return InflatePaths(paths, distance, join, end, kMiterLimit, kPrecision, kArcTolerance);
        }

        double PathLength(const std::span<const Vec2> points) {
            double length = 0.0;
            for (size_t i = 1; i < points.size(); ++i) {
                length += std::hypot(points[i].x - points[i - 1].x, points[i].z - points[i - 1].z);
            }
            return length;
        }

        // Cuts a path into the pieces of a dashed line.
        std::vector<std::vector<Vec2>> SplitIntoDashes(const std::span<const Vec2> path, const Dashes dashes) {
            std::vector<std::vector<Vec2>> pieces;
            if (!(dashes.dashLength > 0.0f) || path.size() < 2) {
                pieces.emplace_back(path.begin(), path.end());
                return pieces;
            }

            const double gap = std::max(0.0f, dashes.gapLength);
            bool drawing = true;
            double left = dashes.dashLength;  // until the current dash or gap ends
            std::vector<Vec2> current{path[0]};
            for (size_t i = 1; i < path.size(); ++i) {
                Vec2 from = path[i - 1];
                const Vec2 to = path[i];
                double segment = std::hypot(to.x - from.x, to.z - from.z);
                while (segment > left) {
                    const double t = left / segment;
                    const Vec2 cut{static_cast<float>(from.x + (to.x - from.x) * t),
                                   static_cast<float>(from.z + (to.z - from.z) * t)};
                    if (drawing) {
                        current.push_back(cut);
                        pieces.push_back(std::move(current));
                    }
                    current = {cut};
                    segment -= left;
                    from = cut;
                    drawing = !drawing;
                    left = drawing ? dashes.dashLength : gap;
                    if (!(left > 0.0)) {
                        drawing = true;
                        left = dashes.dashLength;
                    }
                }
                left -= segment;
                if (drawing) {
                    current.push_back(to);
                }
            }
            if (drawing && current.size() >= 2) {
                pieces.push_back(std::move(current));
            }
            return pieces;
        }
    }

    std::vector<UvVertex> PathUvStrip(const std::span<const Vec2> path, const float halfWidth, const float repeatLength) {
        std::vector<Vec2> points;
        for (const Vec2 p : path) {
            if (points.empty() || std::hypot(p.x - points.back().x, p.z - points.back().z) > 1.0e-4f) {
                points.push_back(p);
            }
        }
        std::vector<UvVertex> strip;
        if (points.size() < 2 || !(halfWidth > 0.0f) || !(repeatLength > 0.0f)) {
            return strip;
        }

        // A little wider than asked, so the strip surely covers a line of that width.
        const float reach = halfWidth * 1.01f;
        const auto direction = [&](const size_t i) {
            const Vec2 a = points[i];
            const Vec2 b = points[i + 1];
            const float length = std::hypot(b.x - a.x, b.z - a.z);
            return Vec2{(b.x - a.x) / length, (b.z - a.z) / length};
        };
        const auto left = [](const Vec2 d) { return Vec2{-d.z, d.x}; };

        struct Row {
            Vec2 centre;
            Vec2 offset;  // to the left edge
            float along;
        };
        std::vector<Row> rows;
        const Vec2 first = direction(0);
        const Vec2 last = direction(points.size() - 2);
        rows.push_back({{points[0].x - first.x * reach, points[0].z - first.z * reach},
                        {left(first).x * reach, left(first).z * reach}, -reach});
        float along = 0.0f;
        for (size_t i = 0; i < points.size(); ++i) {
            if (i > 0) {
                along += std::hypot(points[i].x - points[i - 1].x, points[i].z - points[i - 1].z);
            }
            Vec2 offset;
            if (i == 0 || i + 1 == points.size()) {
                const Vec2 n = left(i == 0 ? first : last);
                offset = {n.x * reach, n.z * reach};
            }
            else {
                // A mitre: the edges stay reach away from both segments. Very sharp turns are
                // capped, where the strip would otherwise reach far out.
                const Vec2 before = left(direction(i - 1));
                const Vec2 after = left(direction(i));
                Vec2 mitre{before.x + after.x, before.z + after.z};
                const float length = std::hypot(mitre.x, mitre.z);
                mitre = length > 1.0e-6f ? Vec2{mitre.x / length, mitre.z / length} : before;
                const float scale = reach / std::max(0.25f, mitre.x * before.x + mitre.z * before.z);
                offset = {mitre.x * scale, mitre.z * scale};
            }
            rows.push_back({points[i], offset, along});
        }
        rows.push_back({{points.back().x + last.x * reach, points.back().z + last.z * reach},
                        {left(last).x * reach, left(last).z * reach}, along + reach});

        const auto corner = [&](const Row& row, const bool leftSide) {
            const float sign = leftSide ? 1.0f : -1.0f;
            return UvVertex{row.centre.x + sign * row.offset.x, row.centre.z + sign * row.offset.z,
                            row.along / repeatLength, leftSide ? 0.0f : 1.0f};
        };
        for (size_t i = 0; i + 1 < rows.size(); ++i) {
            const UvVertex l0 = corner(rows[i], true);
            const UvVertex r0 = corner(rows[i], false);
            const UvVertex l1 = corner(rows[i + 1], true);
            const UvVertex r1 = corner(rows[i + 1], false);
            strip.insert(strip.end(), {l0, r0, r1, l0, r1, l1});
        }
        return strip;
    }

    std::vector<Vec2> PathThrough(const std::span<const Vec2> anchors, const PathStyle style, const bool closed,
                                  const float spacing) {
        std::vector<Vec2> result(anchors.begin(), anchors.end());
        if (style == PathStyle::Straight || anchors.size() < 3) {
            return result;
        }

        // Centripetal Catmull-Rom (alpha 0.5): no loops or cusps between close points.
        std::vector<tinyspline::real> points;
        points.reserve((anchors.size() + 1) * 2);
        for (const Vec2& p : anchors) {
            points.push_back(p.x);
            points.push_back(p.z);
        }
        std::vector<tinyspline::real> first;
        std::vector<tinyspline::real> last;
        if (closed) {
            // Back to the start, with the neighbours on either side of the seam as the end tangents.
            points.push_back(anchors.front().x);
            points.push_back(anchors.front().z);
            first = {anchors.back().x, anchors.back().z};
            last = {anchors[1].x, anchors[1].z};
        }

        const tinyspline::BSpline spline = tinyspline::BSpline::interpolateCatmullRom(
            points, 2, 0.5, closed ? &first : nullptr, closed ? &last : nullptr);

        const double length = PathLength(anchors) + (closed ? std::hypot(anchors.back().x - anchors.front().x,
                                                                         anchors.back().z - anchors.front().z)
                                                            : 0.0);
        const size_t samples = std::clamp<size_t>(static_cast<size_t>(std::ceil(length / std::max(0.05f, spacing))) + 1,
                                                  anchors.size() * 4, 20000);
        const std::vector<tinyspline::real> sampled = spline.sample(samples);

        result.clear();
        result.reserve(sampled.size() / 2);
        for (size_t i = 0; i + 1 < sampled.size(); i += 2) {
            result.push_back({static_cast<float>(sampled[i]), static_cast<float>(sampled[i + 1])});
        }
        if (closed && result.size() > 1 && result.front() == result.back()) {
            result.pop_back();
        }
        // Even sampling puts as many points on gentle stretches as in tight bends. Dropping those
        // within a centimetre of the line between their neighbours changes nothing visible and
        // makes everything built from the path quicker.
        return SimplifyPath(result, 0.01f, closed);
    }

    std::vector<Vec2> SimplifyPath(const std::span<const Vec2> points, const float tolerance, const bool closed) {
        if (points.size() < 3 || !(tolerance > 0.0f)) {
            return {points.begin(), points.end()};
        }
        return ToRing(Clipper2Lib::SimplifyPath(ToPath(points), tolerance, closed));
    }

    Area AreaFromOutline(const std::span<const Vec2> outline, const float cornerRadius) {
        PathsD area = Normalized(PathsD{ToPath(outline)});
        if (cornerRadius > 0.0f && !area.empty()) {
            // Closing rounds the inward corners, opening the outward ones.
            area = Offset(Offset(area, cornerRadius, JoinType::Round, EndType::Polygon), -cornerRadius,
                          JoinType::Round, EndType::Polygon);
            area = Offset(Offset(area, -cornerRadius, JoinType::Round, EndType::Polygon), cornerRadius,
                          JoinType::Round, EndType::Polygon);
        }
        return ToArea(area);
    }

    Area StrokePath(const std::span<const Vec2> path, const float width, const LineEnds ends, const Dashes dashes) {
        if (path.size() < 2 || !(width > 0.0f)) {
            return {};
        }

        PathsD pieces;
        for (const std::vector<Vec2>& piece : SplitIntoDashes(path, dashes)) {
            pieces.push_back(ToPath(piece));
        }
        const bool round = ends == LineEnds::Round;
        const PathsD stroke = Offset(pieces, width * 0.5, round ? JoinType::Round : JoinType::Miter,
                                     round ? EndType::Round : EndType::Butt);
        return ToArea(Union(stroke, FillRule::NonZero, kPrecision));
    }

    Area BrushStroke(const std::span<const Vec2> samples, const float radius) {
        if (samples.empty() || !(radius > 0.0f)) {
            return {};
        }
        // A single point with round ends is a circle.
        const PathsD swept = Offset(PathsD{ToPath(samples)}, radius, JoinType::Round, EndType::Round);
        return ToArea(Union(swept, FillRule::NonZero, kPrecision));
    }

    Area Unite(const Area& a, const Area& b) {
        return ToArea(Union(Normalized(ToPaths(a)), Normalized(ToPaths(b)), FillRule::NonZero, kPrecision));
    }

    Area Subtract(const Area& from, const Area& cut) {
        return ToArea(Difference(Normalized(ToPaths(from)), Normalized(ToPaths(cut)), FillRule::NonZero, kPrecision));
    }

    Area Intersect(const Area& a, const Area& b) {
        return ToArea(Clipper2Lib::Intersect(Normalized(ToPaths(a)), Normalized(ToPaths(b)), FillRule::NonZero, kPrecision));
    }

    Area Grow(const Area& area, const float distance) {
        if (distance == 0.0f) {
            return area;
        }
        return ToArea(Offset(Normalized(ToPaths(area)), distance, JoinType::Round, EndType::Polygon));
    }

    double AreaOf(const Area& area) {
        return std::abs(Clipper2Lib::Area(Normalized(ToPaths(area))));
    }

    bool Contains(const Area& area, const Vec2 point) {
        const PointD p(point.x, point.z);
        bool inside = false;
        for (const Ring& ring : area) {
            if (ring.size() >= 3 && PointInPolygon(p, ToPath(ring)) != PointInPolygonResult::IsOutside) {
                inside = !inside;
            }
        }
        return inside;
    }

    bool IsEmpty(const Area& area) {
        return !(AreaOf(area) > 1.0e-6);
    }

    Bounds BoundsOf(const Area& area) {
        constexpr float kInf = std::numeric_limits<float>::infinity();
        Bounds bounds{{kInf, kInf}, {-kInf, -kInf}};
        for (const Ring& ring : area) {
            for (const Vec2& p : ring) {
                bounds.min = {std::min(bounds.min.x, p.x), std::min(bounds.min.z, p.z)};
                bounds.max = {std::max(bounds.max.x, p.x), std::max(bounds.max.z, p.z)};
            }
        }
        return bounds;
    }

    Area Transformed(const Area& area, const Vec2 pivot, const float rotationRadians, const float scale,
                     const Vec2 offset) {
        const float c = std::cos(rotationRadians) * scale;
        const float s = std::sin(rotationRadians) * scale;
        Area result = area;
        for (Ring& ring : result) {
            for (Vec2& p : ring) {
                const float dx = p.x - pivot.x;
                const float dz = p.z - pivot.z;
                p = {pivot.x + c * dx - s * dz + offset.x, pivot.z + s * dx + c * dz + offset.z};
            }
        }
        return result;
    }
}
