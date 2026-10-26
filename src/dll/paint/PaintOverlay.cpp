#include "PaintOverlay.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>

#include "PaintSettings.hpp"
#include "cISTETerrain.h"

namespace {
    constexpr float kEpsilon = 1e-4f;
    constexpr float kTerrainGridSpacing = 16.0f;

    struct XZPoint {
        float x;
        float z;
    };

    float Cross2D(const XZPoint& a, const XZPoint& b, const XZPoint& c) {
        return (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
    }

    float PolygonAreaSignedXZ(const std::vector<cS3DVector3>& vertices) {
        float area2 = 0.0f;
        const size_t n = vertices.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            area2 += vertices[j].fX * vertices[i].fZ - vertices[i].fX * vertices[j].fZ;
        }
        return area2 * 0.5f;
    }

    bool PointInTriangleXZ(const XZPoint& p, const XZPoint& a, const XZPoint& b, const XZPoint& c) {
        const float c1 = Cross2D(p, a, b);
        const float c2 = Cross2D(p, b, c);
        const float c3 = Cross2D(p, c, a);

        const bool hasNeg = (c1 < -kEpsilon) || (c2 < -kEpsilon) || (c3 < -kEpsilon);
        const bool hasPos = (c1 > kEpsilon) || (c2 > kEpsilon) || (c3 > kEpsilon);
        return !(hasNeg && hasPos);
    }

    cS3DVector3 RotateLocalPoint(const cS3DVector3& local, const int32_t rotation) {
        switch (rotation & 3) {
        case 1:
            return cS3DVector3(-local.fZ, local.fY, local.fX);
        case 2:
            return cS3DVector3(-local.fX, local.fY, -local.fZ);
        case 3:
            return cS3DVector3(local.fZ, local.fY, -local.fX);
        default:
            return local;
        }
    }

    cS3DVector3 RotateLocalPoint(const cS3DVector3& local, const float yawRadians) {
        const float cosYaw = std::cos(yawRadians);
        const float sinYaw = std::sin(yawRadians);
        return cS3DVector3(
            local.fX * cosYaw - local.fZ * sinYaw,
            local.fY,
            local.fX * sinYaw + local.fZ * cosYaw);
    }

    float SampleStableTerrainHeight(cISTETerrain* terrain, const float x, const float z) {
        if (!terrain) {
            return 0.0f;
        }

        const float cellX = std::floor(x / kTerrainGridSpacing);
        const float cellZ = std::floor(z / kTerrainGridSpacing);
        const float x0 = cellX * kTerrainGridSpacing;
        const float z0 = cellZ * kTerrainGridSpacing;
        const float x1 = x0 + kTerrainGridSpacing;
        const float z1 = z0 + kTerrainGridSpacing;
        const float tx = std::clamp((x - x0) / kTerrainGridSpacing, 0.0f, 1.0f);
        const float tz = std::clamp((z - z0) / kTerrainGridSpacing, 0.0f, 1.0f);

        const float h00 = terrain->GetAltitudeAtNearestGrid(x0, z0);
        const float h10 = terrain->GetAltitudeAtNearestGrid(x1, z0);
        const float h01 = terrain->GetAltitudeAtNearestGrid(x0, z1);
        const float h11 = terrain->GetAltitudeAtNearestGrid(x1, z1);
        const float hx0 = h00 + (h10 - h00) * tx;
        const float hx1 = h01 + (h11 - h01) * tx;
        return hx0 + (hx1 - hx0) * tz;
    }
}

PaintOverlay::~PaintOverlay() {
    DestroyLayers_();
}

void PaintOverlay::SetWorldDrawService(cIGZWorldDrawService* service) {
    if (service == worldDraw_) {
        return;
    }
    DestroyLayers_();
    worldDraw_ = service;
    for (auto& layer : layers_) {
        layer.dirty = true;
    }
    Upload_();
}

void PaintOverlay::Clear() {
    ClearLayers_();
    Upload_();
}

bool PaintOverlay::Empty() const {
    for (const auto& layer : layers_) {
        if (!layer.mesh.Empty() || !layer.lines.empty()) {
            return false;
        }
    }
    return true;
}

void PaintOverlay::ClearLayers_() {
    for (auto& layer : layers_) {
        if (!layer.mesh.Empty() || !layer.lines.empty()) {
            layer.mesh.Clear();
            layer.lines.clear();
            layer.dirty = true;
        }
    }
}

void PaintOverlay::Upload_() {
    if (!worldDraw_) {
        return;
    }

    // Drawn at the end of PreDynamic, so hills and buildings hide the preview and moving
    // traffic goes over it. The depth bias keeps it from flickering against the terrain.
    static constexpr SC4WorldStyle kStyle{.depthBias = 8};

    for (uint32_t i = 0; i < layers_.size(); ++i) {
        Layer& layer = layers_[i];
        if (!layer.dirty) {
            continue;
        }

        const bool empty = layer.mesh.Empty() && layer.lines.empty();
        if (layer.id == 0) {
            if (empty) {
                layer.dirty = false;
                continue;
            }
            layer.id = worldDraw_->CreateLayer(DrawServicePass::PreDynamic, kStyle, static_cast<int32_t>(i));
            if (layer.id == 0) {
                // The service cannot draw (yet); keep the geometry and try again next time.
                continue;
            }
        }

        worldDraw_->SetLayerTriangles(layer.id, layer.mesh.Data(), layer.mesh.Count());
        for (const auto& line : layer.lines) {
            worldDraw_->AddLayerLine(layer.id, line.style, line.points.data(),
                                     static_cast<uint32_t>(line.points.size()));
        }
        layer.dirty = false;
    }
}

void PaintOverlay::DestroyLayers_() {
    for (auto& layer : layers_) {
        if (layer.id != 0 && worldDraw_) {
            worldDraw_->DestroyLayer(layer.id);
        }
        layer.id = 0;
    }
}

void PaintOverlay::BuildStripperPreview(const bool cursorValid, const cS3DVector3& cursorPos,
                                             const float pickRadius, cISTETerrain* terrain) {
    BuildStripperPreview(cursorValid, cursorPos, pickRadius, terrain, 0xC0FF3333);
}

void PaintOverlay::BuildStripperPreview(const bool cursorValid, const cS3DVector3& cursorPos,
                                             const float pickRadius, cISTETerrain* terrain, const uint32_t rectColor) {
    ClearLayers_();
    if (!cursorValid) {
        Upload_();
        return;
    }

    const float cx = cursorPos.fX;
    const float cz = cursorPos.fZ;
    const float r = pickRadius;

    // Four corners of the pick rect, terrain-following
    const cS3DVector3 nw(cx - r, SampleStableTerrainHeight(terrain, cx - r, cz - r) + kHeightOffset, cz - r);
    const cS3DVector3 ne(cx + r, SampleStableTerrainHeight(terrain, cx + r, cz - r) + kHeightOffset, cz - r);
    const cS3DVector3 se(cx + r, SampleStableTerrainHeight(terrain, cx + r, cz + r) + kHeightOffset, cz + r);
    const cS3DVector3 sw(cx - r, SampleStableTerrainHeight(terrain, cx - r, cz + r) + kHeightOffset, cz + r);

    EmitScreenLine_({nw, ne, se, sw}, kPickRectWidthPx, rectColor, true, kLayerShape);
    Upload_();
}

void PaintOverlay::AddRectOutline(const float minX, const float minZ, const float maxX, const float maxZ,
                                  cISTETerrain* terrain, const uint32_t color) {
    constexpr float kMaxSegmentLength = 4.0f;

    // Use the game's own continuous altitude query so the outline hugs the
    // rendered (triangulated) mesh; bilinear corner interpolation visibly
    // drifts from the surface inside sloped cells.
    const auto surfaceHeight = [&](const float x, const float z) {
        return terrain ? terrain->GetAltitude(x, z) + kHeightOffset : kHeightOffset;
    };

    // Each edge is cut into short pieces so the outline follows the ground; the corner that
    // ends one edge starts the next, so it is added only once.
    std::vector<cS3DVector3> outline;
    const auto addEdge = [&](const float x0, const float z0, const float x1, const float z1) {
        const float length = std::max(std::abs(x1 - x0), std::abs(z1 - z0));
        const int segments = std::max(1, static_cast<int>(std::ceil(length / kMaxSegmentLength)));
        for (int i = 0; i < segments; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(segments);
            const float x = x0 + (x1 - x0) * t;
            const float z = z0 + (z1 - z0) * t;
            outline.emplace_back(x, surfaceHeight(x, z), z);
        }
    };

    addEdge(minX, minZ, maxX, minZ);
    addEdge(maxX, minZ, maxX, maxZ);
    addEdge(maxX, maxZ, minX, maxZ);
    addEdge(minX, maxZ, minX, minZ);
    EmitScreenLine_(outline, kRectOutlineWidthPx, color, true, kLayerShape);
    Upload_();
}

void PaintOverlay::BuildDirectPreview(const bool cursorValid,
                                          const cS3DVector3& cursorPos,
                                          cISTETerrain* terrain,
                                          const PropPaintSettings& settings,
                                          const PreviewPlacement& plannedPlacement,
                                          const bool drawPlacement) {
    ClearLayers_();
    if (cursorValid) {
        EmitGrid_(cursorPos, terrain, settings);
        if (drawPlacement) {
            EmitPreviewPlacement_(plannedPlacement, terrain, kLayerMarkers);
        }
    }
    Upload_();
}

void PaintOverlay::BuildLinePreview(const std::vector<cS3DVector3>& points,
                                        const std::vector<cS3DVector3>& terrainAnchors,
                                        const cS3DVector3& cursorPos,
                                        const cS3DVector3& cursorTerrainPos,
                                        const bool cursorValid,
                                        cISTETerrain* terrain,
                                        const PropPaintSettings& settings,
                                        const std::vector<PreviewPlacement>& plannedPlacements) {
    ClearLayers_();

    if (cursorValid) {
        EmitGrid_(cursorPos, terrain, settings);
    }

    EmitScreenLine_(points, kPathWidthPx, kLineColor, false, kLayerShape);

    if (!points.empty() && cursorValid) {
        EmitScreenLine_({points.back(), cursorPos}, kPathWidthPx, kCursorColor, false, kLayerShape);
    }

    for (const auto& pt : points) {
        EmitMarker_(pt, kMarkerSize, kMarkerColor, kLayerMarkers);
    }

    for (const auto& placement : plannedPlacements) {
        EmitPreviewPlacement_(placement, terrain, kLayerMarkers);
    }
    Upload_();
}

void PaintOverlay::BuildPolygonPreview(const std::vector<cS3DVector3>& vertices,
                                           const std::vector<cS3DVector3>& terrainAnchors,
                                           const cS3DVector3& cursorPos,
                                           const cS3DVector3& cursorTerrainPos,
                                           const bool cursorValid,
                                           cISTETerrain* terrain,
                                           const PropPaintSettings& settings,
                                           const std::vector<PreviewPlacement>& plannedPlacements) {
    ClearLayers_();

    if (cursorValid) {
        EmitGrid_(cursorPos, terrain, settings);
    }

    EmitScreenLine_(vertices, kPathWidthPx, kLineColor, false, kLayerShape);

    if (!vertices.empty() && cursorValid) {
        EmitScreenLine_({vertices.back(), cursorPos}, kPathWidthPx, kCursorColor, false, kLayerShape);
    }

    if (vertices.size() >= 2 && cursorValid) {
        EmitScreenLine_({cursorPos, vertices.front()}, kCursorCloseWidthPx, kCursorColor, false, kLayerShape);
    }

    if (vertices.size() >= 3) {
        EmitScreenLine_({vertices.back(), vertices.front()}, kPolygonCloseWidthPx, kLineColor, false, kLayerShape);
        EmitFilledPolygon_(vertices, kPolygonFillColor, kLayerShape);
    }

    for (const auto& vertex : vertices) {
        EmitMarker_(vertex, kMarkerSize, kMarkerColor, kLayerMarkers);
    }

    for (const auto& placement : plannedPlacements) {
        EmitPreviewPlacement_(placement, terrain, kLayerMarkers);
    }
    Upload_();
}

void PaintOverlay::EmitGrid_(const cS3DVector3& center, cISTETerrain* terrain, const PropPaintSettings& settings) {
    if (!settings.showGrid) {
        return;
    }

    const float gridStep = std::max(settings.gridStepMeters, 1.0f);
    constexpr float majorStep = 16.0f;
    const float halfSpan = std::max(32.0f, gridStep * 8.0f);
    const float xStart = std::floor((center.fX - halfSpan) / gridStep) * gridStep;
    const float xEnd = std::ceil((center.fX + halfSpan) / gridStep) * gridStep;
    const float zStart = std::floor((center.fZ - halfSpan) / gridStep) * gridStep;
    const float zEnd = std::ceil((center.fZ + halfSpan) / gridStep) * gridStep;

    const auto isMajorLine = [&](const float coordinate) {
        if (gridStep >= majorStep - kEpsilon) {
            return true;
        }

        const float remainder = std::fmod(std::abs(coordinate), majorStep);
        return remainder <= kEpsilon || std::abs(remainder - majorStep) <= kEpsilon;
    };

    // Each grid line follows the terrain with a point every terrain cell length. EmitScreenLine_
    // lifts the points by kHeightOffset on top of kGridHeightExtraOffset.
    std::vector<cS3DVector3> line;
    for (float x = xStart; x <= xEnd + kEpsilon; x += gridStep) {
        line.clear();
        for (float z = zStart;; z += kTerrainGridSpacing) {
            const float clampedZ = std::min(z, zEnd);
            line.emplace_back(x, SampleStableTerrainHeight(terrain, x, clampedZ) + kGridHeightExtraOffset, clampedZ);
            if (clampedZ >= zEnd - kEpsilon) {
                break;
            }
        }
        const bool major = isMajorLine(x);
        EmitScreenLine_(line, major ? kGridMajorWidthPx : kGridMinorWidthPx,
                        major ? kGridMajorColor : kGridMinorColor, false, kLayerGrid);
    }

    for (float z = zStart; z <= zEnd + kEpsilon; z += gridStep) {
        line.clear();
        for (float x = xStart;; x += kTerrainGridSpacing) {
            const float clampedX = std::min(x, xEnd);
            line.emplace_back(clampedX, SampleStableTerrainHeight(terrain, clampedX, z) + kGridHeightExtraOffset, z);
            if (clampedX >= xEnd - kEpsilon) {
                break;
            }
        }
        const bool major = isMajorLine(z);
        EmitScreenLine_(line, major ? kGridMajorWidthPx : kGridMinorWidthPx,
                        major ? kGridMajorColor : kGridMinorColor, false, kLayerGrid);
    }
}

void PaintOverlay::EmitLine_(const cS3DVector3& a, const cS3DVector3& b,
                                 const float thickness, const uint32_t color, const uint32_t layer) {
    if (layer >= layers_.size()) {
        return;
    }

    // A flat band lifted off the surface, or two crossed bands when it runs straight up.
    layers_[layer].dirty = true;
    layers_[layer].mesh.AddLine(a.fX, a.fY + kHeightOffset, a.fZ, b.fX, b.fY + kHeightOffset, b.fZ, thickness, color);
}

void PaintOverlay::EmitScreenLine_(const std::vector<cS3DVector3>& points, const float widthPixels,
                                   const uint32_t color, const bool closed, const uint32_t layer) {
    if (layer >= layers_.size() || points.size() < 2) {
        return;
    }

    ScreenLine line;
    line.style = SC4WorldLineStyle{.width = widthPixels, .unit = SC4WorldLineWidth::Pixels, .closed = closed};
    line.points.reserve(points.size());
    for (const auto& point : points) {
        line.points.push_back({point.fX, point.fY + kHeightOffset, point.fZ, color, 0.0f, 0.0f});
    }
    layers_[layer].lines.push_back(std::move(line));
    layers_[layer].dirty = true;
}

void PaintOverlay::EmitQuad_(const cS3DVector3& a, const cS3DVector3& b, const cS3DVector3& c,
                                 const cS3DVector3& d, const uint32_t color, const uint32_t layer) {
    if (layer >= layers_.size()) {
        return;
    }

    layers_[layer].dirty = true;
    layers_[layer].mesh.AddQuad({a.fX, a.fY, a.fZ, color, 0.0f, 0.0f},
                                {b.fX, b.fY, b.fZ, color, 0.0f, 0.0f},
                                {c.fX, c.fY, c.fZ, color, 0.0f, 0.0f},
                                {d.fX, d.fY, d.fZ, color, 0.0f, 0.0f});
}

void PaintOverlay::EmitMarker_(const cS3DVector3& center, const float size, const uint32_t color,
                                   const uint32_t layer) {
    if (layer >= layers_.size()) {
        return;
    }

    const float half = size * 0.65f;
    const float y = center.fY + kHeightOffset;

    const cS3DVector3 a(center.fX - half, y, center.fZ - half);
    const cS3DVector3 b(center.fX + half, y, center.fZ - half);
    const cS3DVector3 c(center.fX + half, y, center.fZ + half);
    const cS3DVector3 d(center.fX - half, y, center.fZ + half);
    EmitQuad_(a, b, c, d, color, layer);
}

void PaintOverlay::EmitPreviewPlacement_(const PreviewPlacement& preview, cISTETerrain* terrain, const uint32_t layer) {
    const uint32_t markerColor = preview.valid ? kPlannedMarkerColor : kInvalidMarkerColor;
    const uint32_t topColor = preview.valid ? kPlannedBoxTopColor : kInvalidBoxTopColor;
    const uint32_t sideColor = preview.valid ? kPlannedBoxSideColor : kInvalidBoxSideColor;
    const uint32_t stiltColor = preview.valid ? kPlannedStiltColor : kInvalidStiltColor;

    const bool hasBounds = preview.maxX > preview.minX && preview.maxZ > preview.minZ;
    if (!hasBounds && (preview.width <= 0.0f || preview.depth <= 0.0f)) {
        if (terrain) {
            const float groundY =
                SampleStableTerrainHeight(terrain, preview.placement.position.fX, preview.placement.position.fZ) + kHeightOffset;
            const float previewY = preview.placement.position.fY + kHeightOffset;
            if (previewY - groundY > kStiltGapThreshold) {
                EmitLine_(
                    cS3DVector3(preview.placement.position.fX, groundY, preview.placement.position.fZ),
                    cS3DVector3(preview.placement.position.fX, previewY, preview.placement.position.fZ),
                    kLineThickness * 0.4f,
                    stiltColor,
                    layer);
            }
        }
        EmitMarker_(preview.placement.position, kMarkerSize * 0.9f, markerColor, layer);
        return;
    }

    const float minX = hasBounds ? preview.minX : -preview.width * 0.5f;
    const float maxX = hasBounds ? preview.maxX : preview.width * 0.5f;
    const float minY = hasBounds ? preview.minY : 0.0f;
    const float maxY = hasBounds
        ? std::max(preview.maxY, preview.minY + 0.2f)
        : std::max(preview.height, 0.2f);
    const float minZ = hasBounds ? preview.minZ : -preview.depth * 0.5f;
    const float maxZ = hasBounds ? preview.maxZ : preview.depth * 0.5f;

    const auto makeWorldPoint = [&](const float localX, const float localY, const float localZ) {
        const cS3DVector3 local(localX, localY, localZ);
        cS3DVector3 rotated = preview.hasContinuousRotation
            ? RotateLocalPoint(local, preview.continuousRotationRadians)
            : RotateLocalPoint(local, preview.placement.rotation);
        return cS3DVector3(
            preview.placement.position.fX + rotated.fX,
            preview.placement.position.fY + rotated.fY + kHeightOffset,
            preview.placement.position.fZ + rotated.fZ
        );
    };

    const cS3DVector3 baseA = makeWorldPoint(minX, minY, minZ);
    const cS3DVector3 baseB = makeWorldPoint(maxX, minY, minZ);
    const cS3DVector3 baseC = makeWorldPoint(maxX, minY, maxZ);
    const cS3DVector3 baseD = makeWorldPoint(minX, minY, maxZ);

    const cS3DVector3 topA = makeWorldPoint(minX, maxY, minZ);
    const cS3DVector3 topB = makeWorldPoint(maxX, maxY, minZ);
    const cS3DVector3 topC = makeWorldPoint(maxX, maxY, maxZ);
    const cS3DVector3 topD = makeWorldPoint(minX, maxY, maxZ);

    if (terrain) {
        const auto emitStilt = [&](const cS3DVector3& baseCorner) {
            const float groundY = SampleStableTerrainHeight(terrain, baseCorner.fX, baseCorner.fZ) + kHeightOffset;
            if (baseCorner.fY - groundY > kStiltGapThreshold) {
                EmitLine_(
                    cS3DVector3(baseCorner.fX, groundY, baseCorner.fZ),
                    baseCorner,
                    kLineThickness * 0.4f,
                    stiltColor,
                    layer);
            }
        };

        emitStilt(baseA);
        emitStilt(baseB);
        emitStilt(baseC);
        emitStilt(baseD);
    }

    EmitQuad_(topA, topB, topC, topD, topColor, layer);
    EmitQuad_(baseA, baseB, topB, topA, sideColor, layer);
    EmitQuad_(baseB, baseC, topC, topB, sideColor, layer);
    EmitQuad_(baseC, baseD, topD, topC, sideColor, layer);
    EmitQuad_(baseD, baseA, topA, topD, sideColor, layer);

    EmitLine_(baseA, baseB, kLineThickness * 0.7f, markerColor, layer);
    EmitLine_(baseB, baseC, kLineThickness * 0.7f, markerColor, layer);
    EmitLine_(baseC, baseD, kLineThickness * 0.7f, markerColor, layer);
    EmitLine_(baseD, baseA, kLineThickness * 0.7f, markerColor, layer);

    EmitLine_(topA, topB, kLineThickness * 0.55f, markerColor, layer);
    EmitLine_(topB, topC, kLineThickness * 0.55f, markerColor, layer);
    EmitLine_(topC, topD, kLineThickness * 0.55f, markerColor, layer);
    EmitLine_(topD, topA, kLineThickness * 0.55f, markerColor, layer);

    EmitLine_(baseA, topA, kLineThickness * 0.45f, markerColor, layer);
    EmitLine_(baseB, topB, kLineThickness * 0.45f, markerColor, layer);
    EmitLine_(baseC, topC, kLineThickness * 0.45f, markerColor, layer);
    EmitLine_(baseD, topD, kLineThickness * 0.45f, markerColor, layer);

    // Direction arrow on the top face: shaft from mid(C,D) toward mid(A,B), with barbs at the tip.
    const auto midAB = cS3DVector3((topA.fX + topB.fX) * 0.5f, (topA.fY + topB.fY) * 0.5f, (topA.fZ + topB.fZ) * 0.5f);
    const auto midCD = cS3DVector3((topC.fX + topD.fX) * 0.5f, (topC.fY + topD.fY) * 0.5f, (topC.fZ + topD.fZ) * 0.5f);
    const float arrowInset = 0.25f;
    const auto tail = cS3DVector3(
        midCD.fX + (midAB.fX - midCD.fX) * arrowInset, midCD.fY + (midAB.fY - midCD.fY) * arrowInset,
        midCD.fZ + (midAB.fZ - midCD.fZ) * arrowInset);
    const auto tip = cS3DVector3(
        midCD.fX + (midAB.fX - midCD.fX) * (1.0f - arrowInset), midCD.fY + (midAB.fY - midCD.fY) * (1.0f - arrowInset),
        midCD.fZ + (midAB.fZ - midCD.fZ) * (1.0f - arrowInset));

    // Barbs: two lines from the tip angled back toward the far left/right top edges.
    const float barbT = 0.3f;
    const auto barbLeft = cS3DVector3(
        tip.fX + (topD.fX - tip.fX) * barbT, tip.fY + (topD.fY - tip.fY) * barbT,
        tip.fZ + (topD.fZ - tip.fZ) * barbT);
    const auto barbRight = cS3DVector3(
        tip.fX + (topC.fX - tip.fX) * barbT, tip.fY + (topC.fY - tip.fY) * barbT,
        tip.fZ + (topC.fZ - tip.fZ) * barbT);

    constexpr uint32_t kArrowColor = 0xD0FFFFFF;
    EmitLine_(tail, tip, kLineThickness * 0.5f, kArrowColor, layer);
    EmitLine_(tip, barbLeft, kLineThickness * 0.5f, kArrowColor, layer);
    EmitLine_(tip, barbRight, kLineThickness * 0.5f, kArrowColor, layer);
}

void PaintOverlay::EmitFilledPolygon_(const std::vector<cS3DVector3>& vertices,
                                          const uint32_t color, const uint32_t layer) {
    if (layer >= layers_.size() || vertices.size() < 3) {
        return;
    }

    layers_[layer].dirty = true;
    auto& mesh = layers_[layer].mesh;
    const auto emitTriangle = [&](const cS3DVector3& v0, const cS3DVector3& v1, const cS3DVector3& v2) {
        mesh.AddTriangle({v0.fX, v0.fY + kHeightOffset, v0.fZ, color, 0.0f, 0.0f},
                         {v1.fX, v1.fY + kHeightOffset, v1.fZ, color, 0.0f, 0.0f},
                         {v2.fX, v2.fY + kHeightOffset, v2.fZ, color, 0.0f, 0.0f});
    };
    const size_t n = vertices.size();
    std::vector<size_t> indices(n);
    std::iota(indices.begin(), indices.end(), 0);

    const bool ccw = PolygonAreaSignedXZ(vertices) > 0.0f;

    size_t safety = 0;
    while (indices.size() > 3 && safety < n * n) {
        bool earClipped = false;
        const size_t m = indices.size();

        for (size_t i = 0; i < m; ++i) {
            const size_t prev = indices[(i + m - 1) % m];
            const size_t curr = indices[i];
            const size_t next = indices[(i + 1) % m];

            const XZPoint a{vertices[prev].fX, vertices[prev].fZ};
            const XZPoint b{vertices[curr].fX, vertices[curr].fZ};
            const XZPoint c{vertices[next].fX, vertices[next].fZ};
            const float cross = Cross2D(a, b, c);

            if (ccw ? (cross <= kEpsilon) : (cross >= -kEpsilon)) {
                continue;
            }

            bool containsPoint = false;
            for (size_t j = 0; j < m; ++j) {
                const size_t idx = indices[j];
                if (idx == prev || idx == curr || idx == next) {
                    continue;
                }

                const XZPoint p{vertices[idx].fX, vertices[idx].fZ};
                if (PointInTriangleXZ(p, a, b, c)) {
                    containsPoint = true;
                    break;
                }
            }

            if (containsPoint) {
                continue;
            }

            const auto& v0 = vertices[prev];
            const auto& v1 = vertices[curr];
            const auto& v2 = vertices[next];
            emitTriangle(v0, v1, v2);

            indices.erase(indices.begin() + static_cast<std::ptrdiff_t>(i));
            earClipped = true;
            break;
        }

        if (!earClipped) {
            break;
        }
        ++safety;
    }

    if (indices.size() == 3) {
        const auto& v0 = vertices[indices[0]];
        const auto& v1 = vertices[indices[1]];
        const auto& v2 = vertices[indices[2]];
        emitTriangle(v0, v1, v2);
        return;
    }

    // Fallback to a fan if triangulation fails (self-intersections / degenerate input).
    for (size_t i = 1; i + 1 < vertices.size(); ++i) {
        const auto& v0 = vertices[0];
        const auto& v1 = vertices[i];
        const auto& v2 = vertices[i + 1];
        emitTriangle(v0, v1, v2);
    }
}
