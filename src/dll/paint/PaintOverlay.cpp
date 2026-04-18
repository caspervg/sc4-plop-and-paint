#include "PaintOverlay.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>

#include "PaintSettings.hpp"
#include "cISTETerrain.h"

namespace {
    constexpr DWORD kFvf = D3DFVF_XYZ | D3DFVF_DIFFUSE;
    constexpr DWORD kMaxBatchVertices = 60000;
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

void PaintOverlay::Clear() {
    for (auto& layer : layers_) {
        layer.vertices.clear();
    }
}

bool PaintOverlay::Empty() const {
    for (const auto& layer : layers_) {
        if (!layer.vertices.empty()) {
            return false;
        }
    }
    return true;
}

void PaintOverlay::BuildStripperPreview(const bool cursorValid, const cS3DVector3& cursorPos,
                                             const float pickRadius, cISTETerrain* terrain) {
    Clear();
    if (!cursorValid) {
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

    constexpr DWORD kRectColor = 0xC0FF3333;
    constexpr float kThick = 0.5f;
    EmitLine_(nw, ne, kThick, kRectColor, kLayerShape);
    EmitLine_(ne, se, kThick, kRectColor, kLayerShape);
    EmitLine_(se, sw, kThick, kRectColor, kLayerShape);
    EmitLine_(sw, nw, kThick, kRectColor, kLayerShape);

}

void PaintOverlay::BuildDirectPreview(const bool cursorValid,
                                          const cS3DVector3& cursorPos,
                                          cISTETerrain* terrain,
                                          const PropPaintSettings& settings,
                                          const PreviewPlacement& plannedPlacement,
                                          const bool drawPlacement) {
    Clear();
    if (!cursorValid) {
        return;
    }
    EmitGrid_(cursorPos, terrain, settings);
    if (drawPlacement) {
        EmitPreviewPlacement_(plannedPlacement, terrain, kLayerMarkers);
    }
}

void PaintOverlay::BuildLinePreview(const std::vector<cS3DVector3>& points,
                                        const std::vector<cS3DVector3>& terrainAnchors,
                                        const cS3DVector3& cursorPos,
                                        const cS3DVector3& cursorTerrainPos,
                                        const bool cursorValid,
                                        cISTETerrain* terrain,
                                        const PropPaintSettings& settings,
                                        const std::vector<PreviewPlacement>& plannedPlacements) {
    Clear();

    if (cursorValid) {
        EmitGrid_(cursorPos, terrain, settings);
    }

    for (size_t i = 1; i < points.size(); ++i) {
        EmitLine_(points[i - 1], points[i], kLineThickness, kLineColor, kLayerShape);
    }

    if (!points.empty() && cursorValid) {
        EmitLine_(points.back(), cursorPos, kLineThickness, kCursorColor, kLayerShape);
    }

    for (const auto& pt : points) {
        EmitMarker_(pt, kMarkerSize, kMarkerColor, kLayerShape);
    }

    for (const auto& placement : plannedPlacements) {
        EmitPreviewPlacement_(placement, terrain, kLayerMarkers);
    }
}

void PaintOverlay::BuildPolygonPreview(const std::vector<cS3DVector3>& vertices,
                                           const std::vector<cS3DVector3>& terrainAnchors,
                                           const cS3DVector3& cursorPos,
                                           const cS3DVector3& cursorTerrainPos,
                                           const bool cursorValid,
                                           cISTETerrain* terrain,
                                           const PropPaintSettings& settings,
                                           const std::vector<PreviewPlacement>& plannedPlacements) {
    Clear();

    if (cursorValid) {
        EmitGrid_(cursorPos, terrain, settings);
    }

    for (size_t i = 1; i < vertices.size(); ++i) {
        EmitLine_(vertices[i - 1], vertices[i], kLineThickness, kLineColor, kLayerShape);
    }

    if (!vertices.empty() && cursorValid) {
        EmitLine_(vertices.back(), cursorPos, kLineThickness, kCursorColor, kLayerShape);
    }

    if (vertices.size() >= 2 && cursorValid) {
        EmitLine_(cursorPos, vertices.front(), kLineThickness * 0.5f, kCursorColor, kLayerShape);
    }

    if (vertices.size() >= 3) {
        EmitLine_(vertices.back(), vertices.front(), kLineThickness * 0.85f, kLineColor, kLayerShape);
        EmitFilledPolygon_(vertices, kPolygonFillColor, kLayerShape);
    }

    for (const auto& vertex : vertices) {
        EmitMarker_(vertex, kMarkerSize, kMarkerColor, kLayerShape);
    }

    for (const auto& placement : plannedPlacements) {
        EmitPreviewPlacement_(placement, terrain, kLayerMarkers);
    }
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

    for (float x = xStart; x <= xEnd + kEpsilon; x += gridStep) {
        const bool major = isMajorLine(x);
        const DWORD color = major ? kGridMajorColor : kGridMinorColor;
        const float thickness = major ? 0.42f : 0.26f;
        for (float z = zStart; z < zEnd - kEpsilon; z += kTerrainGridSpacing) {
            const float nextZ = std::min(z + kTerrainGridSpacing, zEnd);
            EmitLine_(
                cS3DVector3(x, SampleStableTerrainHeight(terrain, x, z) + kGridHeightExtraOffset, z),
                cS3DVector3(x, SampleStableTerrainHeight(terrain, x, nextZ) + kGridHeightExtraOffset, nextZ),
                thickness,
                color,
                kLayerGrid);
        }
    }

    for (float z = zStart; z <= zEnd + kEpsilon; z += gridStep) {
        const bool major = isMajorLine(z);
        const DWORD color = major ? kGridMajorColor : kGridMinorColor;
        const float thickness = major ? 0.42f : 0.26f;
        for (float x = xStart; x < xEnd - kEpsilon; x += kTerrainGridSpacing) {
            const float nextX = std::min(x + kTerrainGridSpacing, xEnd);
            EmitLine_(
                cS3DVector3(x, SampleStableTerrainHeight(terrain, x, z) + kGridHeightExtraOffset, z),
                cS3DVector3(nextX, SampleStableTerrainHeight(terrain, nextX, z) + kGridHeightExtraOffset, z),
                thickness,
                color,
                kLayerGrid);
        }
    }
}

void PaintOverlay::Draw(IDirect3DDevice7* device, const bool drawGrid) {
    if (!device || Empty()) {
        return;
    }

    SetupRenderState_(device);

    for (size_t i = 0; i < layers_.size(); ++i) {
        if (!drawGrid && i == kLayerGrid) {
            continue;
        }

        const auto& layer = layers_[i];
        if (!layer.visible || layer.vertices.empty()) {
            continue;
        }

        DWORD offset = 0;
        DWORD remaining = static_cast<DWORD>(layer.vertices.size());
        while (remaining > 0) {
            DWORD batch = std::min(remaining, kMaxBatchVertices);
            batch -= batch % 3;
            if (batch == 0) {
                break;
            }

            device->DrawPrimitive(
                D3DPT_TRIANGLELIST,
                kFvf,
                const_cast<OverlayVertex*>(layer.vertices.data() + offset),
                batch,
                D3DDP_WAIT);

            offset += batch;
            remaining -= batch;
        }
    }

    RestoreRenderState_(device);
}

void PaintOverlay::SetupRenderState_(IDirect3DDevice7* device) {
    if (savedState_.texture0) {
        savedState_.texture0->Release();
        savedState_.texture0 = nullptr;
    }

    device->GetRenderState(D3DRENDERSTATE_ZENABLE, &savedState_.zEnable);
    device->GetRenderState(D3DRENDERSTATE_ZWRITEENABLE, &savedState_.zWriteEnable);
    device->GetRenderState(D3DRENDERSTATE_LIGHTING, &savedState_.lighting);
    device->GetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, &savedState_.alphaBlend);
    device->GetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, &savedState_.alphaTest);
    device->GetRenderState(D3DRENDERSTATE_CULLMODE, &savedState_.cullMode);
    device->GetRenderState(D3DRENDERSTATE_FOGENABLE, &savedState_.fogEnable);
    device->GetRenderState(D3DRENDERSTATE_ZBIAS, &savedState_.zBias);
    device->GetRenderState(D3DRENDERSTATE_SRCBLEND, &savedState_.srcBlend);
    device->GetRenderState(D3DRENDERSTATE_DESTBLEND, &savedState_.dstBlend);
    device->GetTextureStageState(0, D3DTSS_COLOROP, &savedState_.colorOp);
    device->GetTextureStageState(0, D3DTSS_COLORARG1, &savedState_.colorArg1);
    device->GetTextureStageState(0, D3DTSS_COLORARG2, &savedState_.colorArg2);
    device->GetTextureStageState(0, D3DTSS_ALPHAOP, &savedState_.alphaOp);
    device->GetTextureStageState(0, D3DTSS_ALPHAARG1, &savedState_.alphaArg1);
    device->GetTextureStageState(0, D3DTSS_ALPHAARG2, &savedState_.alphaArg2);
    device->GetTextureStageState(1, D3DTSS_COLOROP, &savedState_.stage1ColorOp);
    device->GetTextureStageState(1, D3DTSS_ALPHAOP, &savedState_.stage1AlphaOp);
    device->GetTexture(0, &savedState_.texture0);

    device->SetRenderState(D3DRENDERSTATE_ZENABLE, TRUE);
    device->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRENDERSTATE_LIGHTING, FALSE);
    device->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRENDERSTATE_FOGENABLE, FALSE);
    device->SetRenderState(D3DRENDERSTATE_ZBIAS, 8);
    device->SetTexture(0, nullptr);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_CURRENT);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}

void PaintOverlay::RestoreRenderState_(IDirect3DDevice7* device) {
    device->SetRenderState(D3DRENDERSTATE_ZENABLE, savedState_.zEnable);
    device->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, savedState_.zWriteEnable);
    device->SetRenderState(D3DRENDERSTATE_LIGHTING, savedState_.lighting);
    device->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, savedState_.alphaBlend);
    device->SetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, savedState_.alphaTest);
    device->SetRenderState(D3DRENDERSTATE_CULLMODE, savedState_.cullMode);
    device->SetRenderState(D3DRENDERSTATE_FOGENABLE, savedState_.fogEnable);
    device->SetRenderState(D3DRENDERSTATE_ZBIAS, savedState_.zBias);
    device->SetRenderState(D3DRENDERSTATE_SRCBLEND, savedState_.srcBlend);
    device->SetRenderState(D3DRENDERSTATE_DESTBLEND, savedState_.dstBlend);
    device->SetTexture(0, savedState_.texture0);
    device->SetTextureStageState(0, D3DTSS_COLOROP, savedState_.colorOp);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, savedState_.colorArg1);
    device->SetTextureStageState(0, D3DTSS_COLORARG2, savedState_.colorArg2);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, savedState_.alphaOp);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, savedState_.alphaArg1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG2, savedState_.alphaArg2);
    device->SetTextureStageState(1, D3DTSS_COLOROP, savedState_.stage1ColorOp);
    device->SetTextureStageState(1, D3DTSS_ALPHAOP, savedState_.stage1AlphaOp);
    savedState_.texture0 = nullptr;
}

void PaintOverlay::EmitLine_(const cS3DVector3& a, const cS3DVector3& b,
                                 const float thickness, const DWORD color, const uint32_t layer) {
    if (layer >= layers_.size()) {
        return;
    }

    const float dx = b.fX - a.fX;
    const float dy = b.fY - a.fY;
    const float dz = b.fZ - a.fZ;
    const float len = std::sqrt(dx * dx + dz * dz);
    if (len <= kEpsilon) {
        if (std::abs(dy) <= kEpsilon) {
            return;
        }

        const float half = thickness * 0.5f;
        const float ax = a.fX;
        const float az = a.fZ;
        const float bx = b.fX;
        const float bz = b.fZ;
        const float ay = a.fY + kHeightOffset;
        const float by = b.fY + kHeightOffset;

        // Crossed quads keep pure vertical edges visible from more than one angle.
        const cS3DVector3 x0(ax - half, ay, az);
        const cS3DVector3 x1(ax + half, ay, az);
        const cS3DVector3 x2(bx + half, by, bz);
        const cS3DVector3 x3(bx - half, by, bz);
        EmitQuad_(x0, x1, x2, x3, color, layer);

        const cS3DVector3 z0(ax, ay, az - half);
        const cS3DVector3 z1(ax, ay, az + half);
        const cS3DVector3 z2(bx, by, bz + half);
        const cS3DVector3 z3(bx, by, bz - half);
        EmitQuad_(z0, z1, z2, z3, color, layer);
        return;
    }

    const float nx = (-dz / len) * thickness * 0.5f;
    const float nz = (dx / len) * thickness * 0.5f;

    const cS3DVector3 v0(a.fX + nx, a.fY + kHeightOffset, a.fZ + nz);
    const cS3DVector3 v1(a.fX - nx, a.fY + kHeightOffset, a.fZ - nz);
    const cS3DVector3 v2(b.fX - nx, b.fY + kHeightOffset, b.fZ - nz);
    const cS3DVector3 v3(b.fX + nx, b.fY + kHeightOffset, b.fZ + nz);

    EmitQuad_(v0, v1, v2, v3, color, layer);
}

void PaintOverlay::EmitQuad_(const cS3DVector3& a, const cS3DVector3& b, const cS3DVector3& c,
                                 const cS3DVector3& d, const DWORD color, const uint32_t layer) {
    if (layer >= layers_.size()) {
        return;
    }

    auto& verts = layers_[layer].vertices;
    verts.push_back({a.fX, a.fY, a.fZ, color});
    verts.push_back({b.fX, b.fY, b.fZ, color});
    verts.push_back({c.fX, c.fY, c.fZ, color});

    verts.push_back({a.fX, a.fY, a.fZ, color});
    verts.push_back({c.fX, c.fY, c.fZ, color});
    verts.push_back({d.fX, d.fY, d.fZ, color});
}

void PaintOverlay::EmitMarker_(const cS3DVector3& center, const float size, const DWORD color,
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
    const DWORD markerColor = preview.valid ? kPlannedMarkerColor : kInvalidMarkerColor;
    const DWORD topColor = preview.valid ? kPlannedBoxTopColor : kInvalidBoxTopColor;
    const DWORD sideColor = preview.valid ? kPlannedBoxSideColor : kInvalidBoxSideColor;
    const DWORD stiltColor = preview.valid ? kPlannedStiltColor : kInvalidStiltColor;

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

    constexpr DWORD kArrowColor = 0xD0FFFFFF;
    EmitLine_(tail, tip, kLineThickness * 0.5f, kArrowColor, layer);
    EmitLine_(tip, barbLeft, kLineThickness * 0.5f, kArrowColor, layer);
    EmitLine_(tip, barbRight, kLineThickness * 0.5f, kArrowColor, layer);
}

void PaintOverlay::EmitFilledPolygon_(const std::vector<cS3DVector3>& vertices,
                                          const DWORD color, const uint32_t layer) {
    if (layer >= layers_.size() || vertices.size() < 3) {
        return;
    }

    auto& verts = layers_[layer].vertices;
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
            verts.push_back({v0.fX, v0.fY + kHeightOffset, v0.fZ, color});
            verts.push_back({v1.fX, v1.fY + kHeightOffset, v1.fZ, color});
            verts.push_back({v2.fX, v2.fY + kHeightOffset, v2.fZ, color});

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
        verts.push_back({v0.fX, v0.fY + kHeightOffset, v0.fZ, color});
        verts.push_back({v1.fX, v1.fY + kHeightOffset, v1.fZ, color});
        verts.push_back({v2.fX, v2.fY + kHeightOffset, v2.fZ, color});
        return;
    }

    // Fallback to a fan if triangulation fails (self-intersections / degenerate input).
    for (size_t i = 1; i + 1 < vertices.size(); ++i) {
        const auto& v0 = vertices[0];
        const auto& v1 = vertices[i];
        const auto& v2 = vertices[i + 1];
        verts.push_back({v0.fX, v0.fY + kHeightOffset, v0.fZ, color});
        verts.push_back({v1.fX, v1.fY + kHeightOffset, v1.fZ, color});
        verts.push_back({v2.fX, v2.fY + kHeightOffset, v2.fZ, color});
    }
}
