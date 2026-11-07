#include "GroundMarkings.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string_view>
#include <vector>

#include <clipper2/clipper.h>

#include "HersheyFont.hpp"

namespace Ground {
    namespace {
        // Millimetres, as in GroundGeometry.
        constexpr int kPrecision = 3;
        // No marking makes more stalls, stripes or blotches than this.
        constexpr size_t kMaxPieces = 10000;
        constexpr size_t kMaxTextLength = 256;
        constexpr int kArcSegments = 12;  // per quarter turn

        Vec2 operator+(const Vec2 a, const Vec2 b) { return {a.x + b.x, a.z + b.z}; }
        Vec2 operator-(const Vec2 a, const Vec2 b) { return {a.x - b.x, a.z - b.z}; }
        Vec2 operator*(const Vec2 a, const float s) { return {a.x * s, a.z * s}; }

        float Length(const Vec2 v) { return std::hypot(v.x, v.z); }

        Vec2 Normalized(const Vec2 v) {
            const float length = Length(v);
            return length > 1.0e-6f ? v * (1.0f / length) : Vec2{1.0f, 0.0f};
        }

        // Left of the direction, in the top view (x right, z down).
        Vec2 LeftOf(const Vec2 direction) { return {direction.z, -direction.x}; }

        // Collects pieces of paint and joins them into one area. The pieces come from
        // GroundGeometry, whose rings are oriented the Clipper way, so overlaps add up.
        class Pieces {
        public:
            void Add(const Area& area) {
                for (const Ring& ring : area) {
                    Clipper2Lib::PathD path;
                    path.reserve(ring.size());
                    for (const Vec2& p : ring) {
                        path.emplace_back(p.x, p.z);
                    }
                    paths_.push_back(std::move(path));
                }
            }

            void Stroke(std::span<const Vec2> path, const float width, const LineEnds ends = LineEnds::Flat,
                        const Dashes dashes = {}) {
                Add(StrokePath(path, width, ends, dashes));
            }

            void Polygon(std::span<const Vec2> outline) { Add(AreaFromOutline(outline)); }

            [[nodiscard]] Area Joined() const {
                Area area;
                for (const Clipper2Lib::PathD& path :
                     Clipper2Lib::Union(paths_, Clipper2Lib::FillRule::NonZero, kPrecision)) {
                    if (path.size() < 3) {
                        continue;
                    }
                    Ring ring;
                    ring.reserve(path.size());
                    for (const Clipper2Lib::PointD& p : path) {
                        ring.push_back({static_cast<float>(p.x), static_cast<float>(p.y)});
                    }
                    area.push_back(std::move(ring));
                }
                return area;
            }

        private:
            Clipper2Lib::PathsD paths_;
        };

        // A path walked by distance along it.
        class Walk {
        public:
            explicit Walk(std::vector<Vec2> points) : points_(std::move(points)) {
                lengths_.push_back(0.0f);
                for (size_t i = 1; i < points_.size(); ++i) {
                    lengths_.push_back(lengths_.back() + Length(points_[i] - points_[i - 1]));
                }
            }

            [[nodiscard]] float Total() const { return lengths_.back(); }
            [[nodiscard]] const std::vector<Vec2>& Points() const { return points_; }

            // The point at a distance, and the direction of the path there.
            [[nodiscard]] std::pair<Vec2, Vec2> At(const float distance) const {
                const auto it = std::upper_bound(lengths_.begin(), lengths_.end(), distance);
                const size_t i = std::clamp<size_t>(static_cast<size_t>(it - lengths_.begin()), 1, points_.size() - 1);
                const Vec2 a = points_[i - 1];
                const Vec2 b = points_[i];
                const float segment = lengths_[i] - lengths_[i - 1];
                const float t = segment > 0.0f ? std::clamp((distance - lengths_[i - 1]) / segment, 0.0f, 1.0f) : 0.0f;
                return {a + (b - a) * t, Normalized(b - a)};
            }

            // The path moved sideways, to the left for positive distances.
            [[nodiscard]] std::vector<Vec2> Offset(const float distance) const {
                std::vector<Vec2> result;
                result.reserve(points_.size());
                for (size_t i = 0; i < points_.size(); ++i) {
                    const Vec2 in = Normalized(points_[i] - points_[i > 0 ? i - 1 : i]);
                    const Vec2 out = Normalized(points_[std::min(i + 1, points_.size() - 1)] - points_[i]);
                    const Vec2 before = i > 0 ? in : out;
                    const Vec2 after = i + 1 < points_.size() ? out : in;
                    const Vec2 normal = Normalized(LeftOf(before) + LeftOf(after));
                    // Corners keep the distance to both segments, within reason.
                    const float cosHalf = std::max(0.25f, normal.x * LeftOf(after).x + normal.z * LeftOf(after).z);
                    result.push_back(points_[i] + normal * (distance / cosHalf));
                }
                return result;
            }

        private:
            std::vector<Vec2> points_;
            std::vector<float> lengths_;
        };

        std::vector<Vec2> FinitePoints(const std::span<const Vec2> points) {
            std::vector<Vec2> result;
            for (const Vec2& p : points) {
                if (std::isfinite(p.x) && std::isfinite(p.z)) {
                    result.push_back(p);
                }
            }
            return result;
        }

        // ParkingRow and CarPark.
        void AddStalls(const MarkingSpec& spec, const Walk& walk, const float width, Pieces& pieces) {
            const float stallWidth = std::max(0.5f, spec.stallWidth);
            const float depth = std::max(0.5f, spec.stallDepth);
            const float angle = std::clamp(spec.stallAngleDegrees, 30.0f, 90.0f) * std::numbers::pi_v<float> / 180.0f;
            const float spacing = stallWidth / std::sin(angle);
            const float dividerLength = depth / std::sin(angle);
            const size_t dividers =
                std::min(kMaxPieces, static_cast<size_t>(std::floor(walk.Total() / spacing + 1.0e-4f)) + 1);

            const bool carPark = spec.kind == MarkingKind::CarPark;
            const int pairs = carPark ? std::clamp(spec.rowPairs, 1, 50) : 1;
            const float pairStep = 2.0f * depth + std::max(0.0f, spec.aisleWidth);
            const size_t rows = carPark ? static_cast<size_t>(pairs) * 2 : 1;
            const size_t perRow = std::max<size_t>(1, kMaxPieces / rows);

            for (int pair = 0; pair < pairs; ++pair) {
                const float base = static_cast<float>(pair) * pairStep;
                // Each row: where its front is, and which way its stalls point.
                struct Row {
                    float front;
                    float side;
                };
                const Row rowA{base, 1.0f};
                const Row rowB{base + 2.0f * depth, -1.0f};
                for (const Row& row : {rowA, rowB}) {
                    if (row.side < 0.0f && !carPark) {
                        break;
                    }
                    for (size_t i = 0; i < std::min(dividers, perRow); ++i) {
                        const auto [point, direction] = walk.At(static_cast<float>(i) * spacing);
                        const Vec2 left = LeftOf(direction);
                        const Vec2 start = point + left * row.front;
                        const Vec2 lean = direction * std::cos(angle) + left * (std::sin(angle) * row.side);
                        const Vec2 divider[] = {start, start + lean * dividerLength};
                        pieces.Stroke(divider, width);
                    }
                    if (spec.closeFront) {
                        pieces.Stroke(walk.Offset(row.front), width);
                    }
                }
                if (carPark) {
                    // The line between the backs of the two rows.
                    pieces.Stroke(walk.Offset(base + depth), width);
                }
            }
        }

        void AddZebra(const MarkingSpec& spec, const Vec2 from, const Vec2 to, Pieces& pieces) {
            const float across = Length(to - from);
            const float stripeWidth = std::max(0.05f, spec.stripeWidth);
            const float gap = std::max(0.0f, spec.stripeGap);
            const float halfLength = std::max(0.1f, spec.stripeLength) * 0.5f;
            const size_t count = std::min(kMaxPieces, static_cast<size_t>(std::floor((across + gap) / (stripeWidth + gap))));
            if (count == 0) {
                return;
            }
            const Vec2 u = Normalized(to - from);
            const Vec2 along = LeftOf(u);
            const float used = static_cast<float>(count) * stripeWidth + static_cast<float>(count - 1) * gap;
            float position = (across - used) * 0.5f;
            for (size_t i = 0; i < count; ++i, position += stripeWidth + gap) {
                const Vec2 a = from + u * position;
                const Vec2 b = from + u * (position + stripeWidth);
                const Vec2 stripe[] = {a - along * halfLength, b - along * halfLength, b + along * halfLength,
                                       a + along * halfLength};
                pieces.Polygon(stripe);
            }
        }

        void AddDoubleLine(const MarkingSpec& spec, const Walk& walk, const float width, Pieces& pieces) {
            const float offset = (std::max(0.0f, spec.lineSpacing) + width) * 0.5f;
            const Dashes dashes{.dashLength = spec.dashLength, .gapLength = spec.gapLength};
            pieces.Stroke(walk.Offset(offset), width, LineEnds::Flat, dashes);
            pieces.Stroke(walk.Offset(-offset), width, LineEnds::Flat, dashes);
        }

        Area BuildHatching(const MarkingSpec& spec, const std::vector<Vec2>& outline, const PathStyle style,
                           const float width) {
            const Area region = AreaFromOutline(PathThrough(outline, style, true));
            if (IsEmpty(region)) {
                return {};
            }
            const Bounds bounds = BoundsOf(region);
            const Vec2 centre = (bounds.min + bounds.max) * 0.5f;
            const float radius = Length(bounds.max - bounds.min) * 0.5f + width;
            const float spacing = std::max({0.05f, spec.hatchSpacing, width * 1.5f});
            const size_t half = std::min(kMaxPieces / 4, static_cast<size_t>(std::ceil(radius / spacing)));

            // The stripes are at an angle to the first edge, so they turn with the shape.
            const Vec2 axis = Normalized(outline[1] - outline[0]);
            const float angle = spec.hatchAngleDegrees * std::numbers::pi_v<float> / 180.0f;
            const auto turned = [&](const float a) {
                return Vec2{axis.x * std::cos(a) - axis.z * std::sin(a), axis.x * std::sin(a) + axis.z * std::cos(a)};
            };

            const auto stripes = [&](const Vec2 direction) {
                Pieces lines;
                const Vec2 across = LeftOf(direction);
                for (int i = -static_cast<int>(half); i <= static_cast<int>(half); ++i) {
                    const Vec2 middle = centre + across * (static_cast<float>(i) * spacing);
                    const Vec2 line[] = {middle - direction * radius, middle + direction * radius};
                    lines.Stroke(line, width);
                }
                return lines.Joined();
            };

            Area hatch;
            if (spec.chevron) {
                // One family of stripes on each side of the axis through the centre, mirrored.
                const Vec2 side = LeftOf(axis) * (radius * 2.0f);
                const Vec2 far = axis * (radius * 2.0f);
                const Vec2 leftHalf[] = {centre - far, centre + far, centre + far + side, centre - far + side};
                const Vec2 rightHalf[] = {centre - far, centre + far, centre + far - side, centre - far - side};
                Pieces both;
                both.Add(Intersect(stripes(turned(angle)), AreaFromOutline(leftHalf)));
                both.Add(Intersect(stripes(turned(-angle)), AreaFromOutline(rightHalf)));
                hatch = both.Joined();
            }
            else {
                hatch = stripes(turned(angle));
            }

            Pieces pieces;
            pieces.Add(Intersect(hatch, region));
            if (spec.hatchOutline) {
                for (const Ring& ring : region) {
                    // Around and a little past the start, so the ends overlap without a notch.
                    std::vector<Vec2> loop = ring;
                    loop.push_back(ring[0]);
                    loop.push_back(ring[1]);
                    pieces.Stroke(loop, width);
                }
            }
            return pieces.Joined();
        }

        // Arrows are drawn in their own frame: u from tail to tip, v to the left.
        struct ArrowFrame {
            Vec2 origin;
            Vec2 u;
            Vec2 v;

            [[nodiscard]] Vec2 operator()(const float along, const float side) const {
                return origin + u * along + v * side;
            }
        };

        void AddArrowHead(const ArrowFrame& frame, const Vec2 base, const Vec2 direction, const float length,
                          const float halfWidth, Pieces& pieces) {
            const Vec2 side{-direction.z, direction.x};  // either side will do, it is symmetric
            const Vec2 head[] = {frame(base.x + side.x * halfWidth, base.z + side.z * halfWidth),
                                 frame(base.x + direction.x * length, base.z + direction.z * length),
                                 frame(base.x - side.x * halfWidth, base.z - side.z * halfWidth)};
            pieces.Polygon(head);
        }

        // A shaft from (start, 0) that turns a quarter to one side (+1 left, -1 right), with its head.
        void AddTurn(const ArrowFrame& frame, const float start, const float radius, const float side,
                     const float shaft, const float headLength, const float halfWidth, Pieces& pieces) {
            std::vector<Vec2> path;
            for (int i = 0; i <= kArcSegments; ++i) {
                const float a = static_cast<float>(i) / kArcSegments * std::numbers::pi_v<float> * 0.5f;
                path.push_back(frame(start + radius * std::sin(a), side * radius * (1.0f - std::cos(a))));
            }
            pieces.Stroke(path, shaft);
            AddArrowHead(frame, {start + radius, side * radius}, {0.0f, side}, headLength, halfWidth, pieces);
        }

        void AddArrow(const MarkingSpec& spec, const Vec2 tail, const Vec2 tip, const float width, Pieces& pieces) {
            const float length = Length(tip - tail);
            const Vec2 u = Normalized(tip - tail);
            const ArrowFrame frame{tail, u, LeftOf(u)};
            const float halfWidth = std::max(0.1f, spec.arrowWidth) * 0.5f;
            const float shaft = std::max(width, halfWidth * 0.5f);
            const float headLength = std::min(length * 0.35f, halfWidth * 3.0f);

            const auto straight = [&] {
                const float shaftEnd = std::max(0.0f, length - headLength);
                const Vec2 line[] = {frame(0.0f, 0.0f), frame(shaftEnd, 0.0f)};
                pieces.Stroke(line, shaft);
                AddArrowHead(frame, {shaftEnd, 0.0f}, {1.0f, 0.0f}, headLength, halfWidth, pieces);
            };
            // A turn on its own bends near the end; next to a straight arrow it branches off early.
            const auto turn = [&](const float side, const bool branch) {
                const float radius = length * 0.3f;
                const float start = branch ? length * 0.3f : std::max(0.0f, length - radius - halfWidth);
                if (!branch) {
                    const Vec2 line[] = {frame(0.0f, 0.0f), frame(start, 0.0f)};
                    pieces.Stroke(line, shaft);
                }
                AddTurn(frame, start, radius, side, shaft, headLength, halfWidth, pieces);
            };

            switch (spec.arrow) {
            case ArrowKind::Straight:
                straight();
                break;
            case ArrowKind::Left:
                turn(1.0f, false);
                break;
            case ArrowKind::Right:
                turn(-1.0f, false);
                break;
            case ArrowKind::StraightLeft:
                straight();
                turn(1.0f, true);
                break;
            case ArrowKind::StraightRight:
                straight();
                turn(-1.0f, true);
                break;
            case ArrowKind::LeftRight: {
                const Vec2 line[] = {frame(0.0f, 0.0f), frame(length * 0.3f, 0.0f)};
                pieces.Stroke(line, shaft);
                turn(1.0f, true);
                turn(-1.0f, true);
                break;
            }
            case ArrowKind::UTurn: {
                // Up, round to the left, and back down a little.
                const float radius = std::max(halfWidth * 1.2f, length * 0.15f);
                const float top = std::max(0.0f, length - radius);
                std::vector<Vec2> path{frame(0.0f, 0.0f)};
                for (int i = 0; i <= kArcSegments * 2; ++i) {
                    const float a = static_cast<float>(i) / (kArcSegments * 2) * std::numbers::pi_v<float>;
                    path.push_back(frame(top + radius * std::sin(a), radius * (1.0f - std::cos(a))));
                }
                const float back = std::max(0.0f, top - length * 0.2f);
                path.push_back(frame(back, 2.0f * radius));
                pieces.Stroke(path, shaft);
                AddArrowHead(frame, {back, 2.0f * radius}, {-1.0f, 0.0f}, headLength, halfWidth, pieces);
                break;
            }
            }
        }

        void AddText(const MarkingSpec& spec, const Vec2 start, const Vec2 towards, const float width, Pieces& pieces) {
            const Vec2 along = Normalized(towards - start);
            // Glyph y grows downwards, which is to the right of the baseline in the top view.
            const Vec2 down = LeftOf(along) * -1.0f;
            const float scale = std::max(0.1f, spec.textHeight) / Hershey::kCapHeight;
            const float spacing = std::clamp(spec.letterSpacing, -0.5f, 2.0f) * Hershey::kCapHeight;

            float pen = 0.0f;
            const std::string_view text = std::string_view(spec.text).substr(0, kMaxTextLength);
            for (const char c : text) {
                const char* glyph = Hershey::Glyph(c);
                if (!glyph) {
                    glyph = Hershey::Glyph(' ');
                }
                const int left = glyph[0] - 'R';
                const int right = glyph[1] - 'R';
                std::vector<Vec2> stroke;
                const auto flush = [&] {
                    if (stroke.size() >= 2) {
                        pieces.Stroke(stroke, width, LineEnds::Round);
                    }
                    stroke.clear();
                };
                for (const char* p = glyph + 2; p[0] && p[1]; p += 2) {
                    if (p[0] == ' ' && p[1] == 'R') {
                        flush();
                        continue;
                    }
                    const float x = pen + static_cast<float>(p[0] - 'R' - left);
                    const float y = static_cast<float>(p[1] - 'R' - Hershey::kBaseline);
                    stroke.push_back(start + along * (x * scale) + down * (y * scale));
                }
                flush();
                pen += static_cast<float>(right - left) + spacing;
            }
        }

        double Perimeter(const Area& area) {
            double total = 0.0;
            for (const Ring& ring : area) {
                for (size_t i = 0; i < ring.size(); ++i) {
                    total += Length(ring[(i + 1) % ring.size()] - ring[i]);
                }
            }
            return total;
        }
    }

    MarkingPoints PointsFor(const MarkingKind kind) {
        switch (kind) {
        case MarkingKind::Zebra:
        case MarkingKind::Arrow:
        case MarkingKind::Text:
            return {2, 2, false};
        case MarkingKind::Hatching:
            return {3, 0, true};
        case MarkingKind::ParkingRow:
        case MarkingKind::CarPark:
        case MarkingKind::DoubleLine:
            break;
        }
        return {2, 0, false};
    }

    Area BuildMarking(const MarkingSpec& spec, const std::span<const Vec2> points, const PathStyle pathStyle) {
        const std::vector<Vec2> finite = FinitePoints(points);
        const MarkingPoints needed = PointsFor(spec.kind);
        if (finite.size() < needed.minPoints) {
            return {};
        }
        const float width = std::clamp(spec.lineWidth, 0.01f, 5.0f);
        const Vec2 first = finite.front();
        const Vec2 second = finite[1];

        Pieces pieces;
        switch (spec.kind) {
        case MarkingKind::ParkingRow:
        case MarkingKind::CarPark:
        case MarkingKind::DoubleLine: {
            const Walk walk(PathThrough(finite, pathStyle, false));
            if (!(walk.Total() > 0.0f)) {
                return {};
            }
            if (spec.kind == MarkingKind::DoubleLine) {
                AddDoubleLine(spec, walk, width, pieces);
            }
            else {
                AddStalls(spec, walk, width, pieces);
            }
            break;
        }
        case MarkingKind::Zebra:
            AddZebra(spec, first, second, pieces);
            break;
        case MarkingKind::Arrow:
            if (!(Length(second - first) > 0.0f)) {
                return {};
            }
            AddArrow(spec, first, second, width, pieces);
            break;
        case MarkingKind::Hatching:
            return BuildHatching(spec, finite, pathStyle, width);
        case MarkingKind::Text:
            if (!(Length(second - first) > 0.0f)) {
                return {};
            }
            AddText(spec, first, second, width, pieces);
            break;
        }
        return pieces.Joined();
    }

    float WearPatchSize(const Area& area) {
        const double paint = AreaOf(area);
        if (!(paint > 0.0)) {
            return 0.5f;
        }
        // Patches about the size of the paint's thickness, so thin lines wear through in places
        // and wide areas get scuffed rather than holed.
        const auto thickness = static_cast<float>(2.0 * paint / std::max(1.0e-3, Perimeter(area)));
        return 2.0f * std::clamp(thickness * 0.75f, 0.04f, 0.6f);
    }

    const char* MarkingKindName(const MarkingKind kind) {
        switch (kind) {
        case MarkingKind::ParkingRow:
            return "Parking row";
        case MarkingKind::CarPark:
            return "Car park";
        case MarkingKind::Zebra:
            return "Zebra crossing";
        case MarkingKind::Arrow:
            return "Arrow";
        case MarkingKind::DoubleLine:
            return "Double line";
        case MarkingKind::Hatching:
            return "Hatching";
        case MarkingKind::Text:
            return "Text";
        }
        return "Marking";
    }
}
