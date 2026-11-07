#include "GroundGeometry.hpp"

#include <algorithm>
#include <array>
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
        // Crossings of a ray to +x, counted over all rings: the even-odd rule.
        bool inside = false;
        for (const Ring& ring : area) {
            const size_t n = ring.size();
            if (n < 3) {
                continue;
            }
            for (size_t i = 0, j = n - 1; i < n; j = i++) {
                const Vec2 a = ring[i];
                const Vec2 b = ring[j];
                if ((a.z > point.z) != (b.z > point.z) &&
                    point.x < (b.x - a.x) * (point.z - a.z) / (b.z - a.z) + a.x) {
                    inside = !inside;
                }
            }
        }
        return inside;
    }

    bool Overlaps(const Bounds& a, const Bounds& b) {
        return a.min.x <= b.max.x && b.min.x <= a.max.x && a.min.z <= b.max.z && b.min.z <= a.max.z;
    }

    bool Contains(const Bounds& bounds, const Vec2 point, const float margin) {
        return point.x >= bounds.min.x - margin && point.x <= bounds.max.x + margin && point.z >= bounds.min.z - margin &&
               point.z <= bounds.max.z + margin;
    }

    double PerimeterOf(const Area& area) {
        double total = 0.0;
        for (const Ring& ring : area) {
            for (size_t i = 0; i < ring.size(); ++i) {
                const Vec2 a = ring[i];
                const Vec2 b = ring[(i + 1) % ring.size()];
                total += std::hypot(b.x - a.x, b.z - a.z);
            }
        }
        return total;
    }

    Area BorderOf(const Area& area, const float width, const BorderSide side) {
        if (!(width > 0.0f) || area.empty()) {
            return {};
        }
        const PathsD paths = Normalized(ToPaths(area));
        // Mitred, so square corners stay square.
        const auto grown = [&](const double distance) {
            return Offset(paths, distance, JoinType::Miter, EndType::Polygon);
        };
        switch (side) {
        case BorderSide::Outside:
            return ToArea(Difference(grown(width), paths, FillRule::NonZero, kPrecision));
        case BorderSide::Inside:
            return ToArea(Difference(paths, grown(-width), FillRule::NonZero, kPrecision));
        case BorderSide::Centred:
            return ToArea(Difference(grown(width * 0.5), grown(-width * 0.5), FillRule::NonZero, kPrecision));
        }
        return {};
    }

    namespace {
        uint32_t Hash(const int32_t x, const int32_t z, const uint32_t seed) {
            uint32_t h = seed * 0x9E3779B1u;
            h ^= static_cast<uint32_t>(x) * 0x85EBCA77u;
            h = (h << 13) | (h >> 19);
            h ^= static_cast<uint32_t>(z) * 0xC2B2AE3Du;
            h ^= h >> 16;
            h *= 0x7FEB352Du;
            h ^= h >> 15;
            h *= 0x846CA68Bu;
            h ^= h >> 16;
            return h;
        }

        // -1 to 1 at the lattice points, smoothly in between.
        float ValueNoise(const float x, const float z, const uint32_t seed) {
            const float fx = std::floor(x);
            const float fz = std::floor(z);
            const auto ix = static_cast<int32_t>(fx);
            const auto iz = static_cast<int32_t>(fz);
            const float tx = x - fx;
            const float tz = z - fz;
            const float sx = tx * tx * (3.0f - 2.0f * tx);
            const float sz = tz * tz * (3.0f - 2.0f * tz);
            const auto corner = [&](const int32_t dx, const int32_t dz) {
                return static_cast<float>(Hash(ix + dx, iz + dz, seed) & 0xFFFFFFu) / 8388607.5f - 1.0f;
            };
            const float top = corner(0, 0) + (corner(1, 0) - corner(0, 0)) * sx;
            const float bottom = corner(0, 1) + (corner(1, 1) - corner(0, 1)) * sx;
            return top + (bottom - top) * sz;
        }

        // A pattern frame with its turn worked out once, for sampling many points.
        class FrameSampler {
        public:
            FrameSampler(const PatternFrame& frame, const float featureSize)
                : origin_(frame.origin)
                , scale_(1.0f / std::max(0.05f, featureSize))
                , c_(std::cos(frame.turnRadians) * scale_)
                , s_(std::sin(frame.turnRadians) * scale_) {}

            [[nodiscard]] float operator()(const Vec2 point, const uint32_t seed) const;

        private:
            Vec2 origin_;
            float scale_;
            float c_;
            float s_;
        };

        // Three octaves, each half as strong and twice as fine.
        float Fractal(const float x, const float z, const uint32_t seed) {
            return (ValueNoise(x, z, seed) + 0.5f * ValueNoise(x * 2.03f + 17.1f, z * 2.03f - 3.7f, seed + 1) +
                    0.25f * ValueNoise(x * 4.07f - 9.3f, z * 4.07f + 11.9f, seed + 2)) /
                   1.75f;
        }
    }

    float FrameSampler::operator()(const Vec2 point, const uint32_t seed) const {
        const float dx = point.x - origin_.x;
        const float dz = point.z - origin_.z;
        return Fractal(c_ * dx + s_ * dz, -s_ * dx + c_ * dz, seed);
    }

    float PatternNoise(const Vec2 point, const PatternFrame& frame, const float featureSize, const uint32_t seed) {
        return FrameSampler(frame, featureSize)(point, seed);
    }

    Area Roughened(const Area& area, const float amount, const PatternFrame& frame, const uint32_t seed) {
        if (!(amount > 0.0f) || area.empty()) {
            return area;
        }
        // Points about every quarter feature, but never so many that a big shape gets slow.
        constexpr double kMaxPoints = 40000.0;
        const float feature = amount * 3.0f;
        const double perimeter = PerimeterOf(area);
        const auto spacing = static_cast<float>(std::max<double>({0.15, feature * 0.25, perimeter / kMaxPoints}));

        const FrameSampler noise(frame, feature);
        PathsD rough;
        rough.reserve(area.size());
        for (const Ring& ring : area) {
            PathD path;
            for (size_t i = 0; i < ring.size(); ++i) {
                const Vec2 a = ring[i];
                const Vec2 b = ring[(i + 1) % ring.size()];
                const int pieces = std::max(1, static_cast<int>(std::ceil(std::hypot(b.x - a.x, b.z - a.z) / spacing)));
                for (int k = 0; k < pieces; ++k) {
                    const float t = static_cast<float>(k) / static_cast<float>(pieces);
                    const Vec2 p{a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t};
                    // Two independent noises push the point sideways and along.
                    path.emplace_back(p.x + amount * noise(p, seed), p.z + amount * noise(p, seed + 101));
                }
            }
            if (path.size() >= 3) {
                rough.push_back(std::move(path));
            }
        }
        return ToArea(Normalized(rough));
    }

    Area PatchesOf(const Area& area, const float patchSize, const float coverage, const PatternFrame& frame,
                   const uint32_t seed, const size_t maxCells) {
        if (area.empty() || !(coverage > 0.0f)) {
            return {};
        }
        if (coverage >= 1.0f) {
            return area;
        }
        const Bounds bounds = BoundsOf(area);
        const float width = bounds.max.x - bounds.min.x;
        const float depth = bounds.max.z - bounds.min.z;
        if (!(width > 0.0f) || !(depth > 0.0f)) {
            return {};
        }
        // A sixth of a patch per cell draws its outline smoothly; big areas get coarser cells.
        float cell = std::max(0.25f, patchSize / 6.0f);
        const auto cellsFor = [&](const float size) {
            return (static_cast<double>(std::ceil(width / size)) + 1.0) * (static_cast<double>(std::ceil(depth / size)) + 1.0);
        };
        while (cellsFor(cell) > static_cast<double>(std::max<size_t>(64, maxCells))) {
            cell *= 1.25f;
        }
        const auto columns = static_cast<size_t>(std::ceil(width / cell)) + 1;
        const auto rows = static_cast<size_t>(std::ceil(depth / cell)) + 1;
        const auto at = [&](const size_t column, const size_t row) {
            return Vec2{bounds.min.x + static_cast<float>(column) * cell, bounds.min.z + static_cast<float>(row) * cell};
        };

        const FrameSampler noise(frame, patchSize);
        std::vector<float> values((columns + 1) * (rows + 1));
        for (size_t row = 0; row <= rows; ++row) {
            for (size_t column = 0; column <= columns; ++column) {
                values[row * (columns + 1) + column] = noise(at(column, row), seed);
            }
        }
        // The level above which the given share of the ground lies.
        std::vector<float> sorted = values;
        const auto nth = sorted.begin() + static_cast<std::ptrdiff_t>(
                                              std::clamp((1.0f - coverage) * static_cast<float>(sorted.size() - 1), 0.0f,
                                                         static_cast<float>(sorted.size() - 1)));
        std::nth_element(sorted.begin(), nth, sorted.end());
        const float level = *nth;

        // Marching squares that links the crossings into closed outlines as it goes, so no union
        // of thousands of cell pieces is needed. The grid gets a border below the level, so every
        // outline closes.
        const size_t stride = columns + 3;  // corners per padded row
        const size_t height = rows + 3;
        const auto padded = [&](const size_t column, const size_t row) {
            if (column == 0 || row == 0 || column > columns + 1 || row > rows + 1) {
                return level - 1.0f;
            }
            return values[(row - 1) * (columns + 1) + (column - 1)];
        };
        const auto corner = [&](const size_t column, const size_t row) {
            return Vec2{bounds.min.x + (static_cast<float>(column) - 1.0f) * cell,
                        bounds.min.z + (static_cast<float>(row) - 1.0f) * cell};
        };
        // Edges: 2 per corner, horizontal (to the right) and vertical (down).
        const auto horizontal = [&](const size_t column, const size_t row) {
            return static_cast<uint32_t>(2 * (row * stride + column));
        };
        const auto vertical = [&](const size_t column, const size_t row) {
            return static_cast<uint32_t>(2 * (row * stride + column) + 1);
        };
        const auto crossing = [&](const uint32_t edge) {
            const size_t index = edge / 2;
            const size_t column = index % stride;
            const size_t row = index / stride;
            const size_t column2 = (edge & 1) ? column : column + 1;
            const size_t row2 = (edge & 1) ? row + 1 : row;
            const float v0 = padded(column, row);
            const float v1 = padded(column2, row2);
            const float t = std::clamp((level - v0) / (v1 - v0), 0.0f, 1.0f);
            const Vec2 p = corner(column, row);
            const Vec2 q = corner(column2, row2);
            return PointD(p.x + (q.x - p.x) * t, p.z + (q.z - p.z) * t);
        };

        constexpr uint32_t kNone = UINT32_MAX;
        std::vector<std::array<uint32_t, 2>> links(2 * stride * height, {kNone, kNone});
        const auto link = [&](const uint32_t a, const uint32_t b) {
            links[a][links[a][0] == kNone ? 0 : 1] = b;
            links[b][links[b][0] == kNone ? 0 : 1] = a;
        };
        for (size_t row = 0; row + 1 < height; ++row) {
            for (size_t column = 0; column + 1 < stride; ++column) {
                const bool tl = padded(column, row) > level;
                const bool tr = padded(column + 1, row) > level;
                const bool br = padded(column + 1, row + 1) > level;
                const bool bl = padded(column, row + 1) > level;
                const int pattern = (tl ? 8 : 0) | (tr ? 4 : 0) | (br ? 2 : 0) | (bl ? 1 : 0);
                if (pattern == 0 || pattern == 15) {
                    continue;
                }
                const uint32_t top = horizontal(column, row);
                const uint32_t bottom = horizontal(column, row + 1);
                const uint32_t left = vertical(column, row);
                const uint32_t right = vertical(column + 1, row);
                switch (pattern) {
                case 1: case 14: link(left, bottom); break;
                case 2: case 13: link(bottom, right); break;
                case 3: case 12: link(left, right); break;
                case 4: case 11: link(top, right); break;
                case 6: case 9: link(top, bottom); break;
                case 7: case 8: link(left, top); break;
                case 5: case 10: {
                    // A saddle: the middle decides which corners join.
                    const float middle = (padded(column, row) + padded(column + 1, row) + padded(column + 1, row + 1) +
                                          padded(column, row + 1)) * 0.25f;
                    if ((middle > level) == (pattern == 5)) {
                        link(left, top);
                        link(bottom, right);
                    }
                    else {
                        link(left, bottom);
                        link(top, right);
                    }
                    break;
                }
                default:
                    break;
                }
            }
        }

        PathsD outlines;
        std::vector<bool> used(links.size(), false);
        for (uint32_t start = 0; start < links.size(); ++start) {
            if (used[start] || links[start][0] == kNone) {
                continue;
            }
            PathD outline;
            uint32_t previous = kNone;
            uint32_t current = start;
            while (current != kNone && !used[current]) {
                used[current] = true;
                outline.push_back(crossing(current));
                const uint32_t next = links[current][0] != previous ? links[current][0] : links[current][1];
                previous = current;
                current = next;
            }
            if (outline.size() >= 3) {
                outlines.push_back(std::move(outline));
            }
        }
        // Outlines never cross, so even-odd reads them right whichever way round they go.
        const PathsD simple = SimplifyPaths(outlines, cell * 0.05, true);
        return ToArea(Clipper2Lib::Intersect(simple, Normalized(ToPaths(area)), FillRule::EvenOdd, kPrecision));
    }

    Vec2 NearestOnSegment(const Vec2 p, const Vec2 a, const Vec2 b) {
        const float dx = b.x - a.x;
        const float dz = b.z - a.z;
        const float lengthSquared = dx * dx + dz * dz;
        const float t =
            lengthSquared > 0.0f ? std::clamp(((p.x - a.x) * dx + (p.z - a.z) * dz) / lengthSquared, 0.0f, 1.0f) : 0.0f;
        return {a.x + t * dx, a.z + t * dz};
    }

    Vec2 MirroredPoint(const Vec2 point, const Vec2 pivot, const float axisRadians) {
        const float c = std::cos(2.0f * axisRadians);
        const float s = std::sin(2.0f * axisRadians);
        const float dx = point.x - pivot.x;
        const float dz = point.z - pivot.z;
        return {pivot.x + c * dx + s * dz, pivot.z + s * dx - c * dz};
    }

    Area Mirrored(const Area& area, const Vec2 pivot, const float axisRadians) {
        Area result = area;
        for (Ring& ring : result) {
            for (Vec2& p : ring) {
                p = MirroredPoint(p, pivot, axisRadians);
            }
            // A mirror turns the rings round; keep them the way the rest of the code makes them.
            std::ranges::reverse(ring);
        }
        return result;
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
