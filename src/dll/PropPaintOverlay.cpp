#include "PropPaintOverlay.hpp"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <numeric>

namespace {
    constexpr DWORD kFvf = D3DFVF_XYZ | D3DFVF_DIFFUSE;
    constexpr DWORD kMaxBatchVertices = 60000;
    constexpr float kEpsilon = 1e-4f;

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
}

void PropPaintOverlay::Clear() {
    for (auto& layer : layers_) {
        layer.vertices.clear();
    }
}

bool PropPaintOverlay::Empty() const {
    for (const auto& layer : layers_) {
        if (!layer.vertices.empty()) {
            return false;
        }
    }
    return true;
}

void PropPaintOverlay::BuildLinePreview(const std::vector<cS3DVector3>& points,
                                        const cS3DVector3& cursorPos,
                                        const bool cursorValid,
                                        const std::vector<cS3DVector3>& plannedPositions) {
    Clear();

    for (size_t i = 1; i < points.size(); ++i) {
        EmitLine_(points[i - 1], points[i], kLineThickness, kLineColor, kLayerShape);
    }

    if (!points.empty() && cursorValid) {
        EmitLine_(points.back(), cursorPos, kLineThickness, kCursorColor, kLayerShape);
    }

    for (const auto& pt : points) {
        EmitMarker_(pt, kMarkerSize, kMarkerColor, kLayerShape);
    }

    for (const auto& pos : plannedPositions) {
        EmitMarker_(pos, kMarkerSize * 0.9f, kPlannedMarkerColor, kLayerMarkers);
    }
}

void PropPaintOverlay::BuildPolygonPreview(const std::vector<cS3DVector3>& vertices,
                                           const cS3DVector3& cursorPos,
                                           const bool cursorValid,
                                           const std::vector<cS3DVector3>& plannedPositions) {
    Clear();

    for (size_t i = 1; i < vertices.size(); ++i) {
        EmitLine_(vertices[i - 1], vertices[i], kLineThickness, kLineColor, kLayerShape);
    }

    if (vertices.size() >= 2 && cursorValid) {
        EmitLine_(vertices.back(), cursorPos, kLineThickness, kCursorColor, kLayerShape);
        EmitLine_(cursorPos, vertices.front(), kLineThickness * 0.5f, kCursorColor, kLayerShape);
    }

    if (vertices.size() >= 3) {
        EmitLine_(vertices.back(), vertices.front(), kLineThickness * 0.85f, kLineColor, kLayerShape);
        EmitFilledPolygon_(vertices, kPolygonFillColor, kLayerShape);
    }

    for (const auto& vertex : vertices) {
        EmitMarker_(vertex, kMarkerSize, kMarkerColor, kLayerShape);
    }

    for (const auto& pos : plannedPositions) {
        EmitMarker_(pos, kMarkerSize * 0.9f, kPlannedMarkerColor, kLayerMarkers);
    }
}

void PropPaintOverlay::Draw(IDirect3DDevice7* device) {
    if (!device || Empty()) {
        return;
    }

    SetupRenderState_(device);

    for (const auto& layer : layers_) {
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

void PropPaintOverlay::SetupRenderState_(IDirect3DDevice7* device) {
    device->GetRenderState(D3DRENDERSTATE_ZENABLE, &savedState_.zEnable);
    device->GetRenderState(D3DRENDERSTATE_ZWRITEENABLE, &savedState_.zWriteEnable);
    device->GetRenderState(D3DRENDERSTATE_LIGHTING, &savedState_.lighting);
    device->GetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, &savedState_.alphaBlend);
    device->GetRenderState(D3DRENDERSTATE_CULLMODE, &savedState_.cullMode);
    device->GetRenderState(D3DRENDERSTATE_ZBIAS, &savedState_.zBias);
    device->GetRenderState(D3DRENDERSTATE_SRCBLEND, &savedState_.srcBlend);
    device->GetRenderState(D3DRENDERSTATE_DESTBLEND, &savedState_.dstBlend);
    device->GetTextureStageState(0, D3DTSS_COLOROP, &savedState_.colorOp);
    device->GetTextureStageState(0, D3DTSS_COLORARG1, &savedState_.colorArg1);
    device->GetTextureStageState(0, D3DTSS_ALPHAOP, &savedState_.alphaOp);
    device->GetTextureStageState(0, D3DTSS_ALPHAARG1, &savedState_.alphaArg1);

    device->SetRenderState(D3DRENDERSTATE_ZENABLE, TRUE);
    device->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRENDERSTATE_LIGHTING, FALSE);
    device->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRENDERSTATE_ZBIAS, 8);
    device->SetTexture(0, nullptr);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
}

void PropPaintOverlay::RestoreRenderState_(IDirect3DDevice7* device) {
    device->SetRenderState(D3DRENDERSTATE_ZENABLE, savedState_.zEnable);
    device->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, savedState_.zWriteEnable);
    device->SetRenderState(D3DRENDERSTATE_LIGHTING, savedState_.lighting);
    device->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, savedState_.alphaBlend);
    device->SetRenderState(D3DRENDERSTATE_CULLMODE, savedState_.cullMode);
    device->SetRenderState(D3DRENDERSTATE_ZBIAS, savedState_.zBias);
    device->SetRenderState(D3DRENDERSTATE_SRCBLEND, savedState_.srcBlend);
    device->SetRenderState(D3DRENDERSTATE_DESTBLEND, savedState_.dstBlend);
    device->SetTextureStageState(0, D3DTSS_COLOROP, savedState_.colorOp);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, savedState_.colorArg1);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, savedState_.alphaOp);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, savedState_.alphaArg1);
}

void PropPaintOverlay::EmitLine_(const cS3DVector3& a, const cS3DVector3& b,
                                 const float thickness, const DWORD color, const uint32_t layer) {
    if (layer >= layers_.size()) {
        return;
    }

    const float dx = b.fX - a.fX;
    const float dz = b.fZ - a.fZ;
    const float len = std::sqrt(dx * dx + dz * dz);
    if (len <= kEpsilon) {
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

void PropPaintOverlay::EmitQuad_(const cS3DVector3& a, const cS3DVector3& b, const cS3DVector3& c,
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

void PropPaintOverlay::EmitMarker_(const cS3DVector3& center, const float size, const DWORD color,
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

void PropPaintOverlay::EmitFilledPolygon_(const std::vector<cS3DVector3>& vertices,
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
