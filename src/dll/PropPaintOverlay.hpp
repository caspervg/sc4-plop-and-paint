#pragma once
#define WIN32_LEAN_AND_MEAN

#include <array>
#include <cstdint>
#include <vector>

#include <d3d.h>

#include "cS3DVector3.h"

class PropPaintOverlay {
public:
    static constexpr uint32_t kLayerShape = 0;
    static constexpr uint32_t kLayerMarkers = 1;

    void Clear();
    [[nodiscard]] bool Empty() const;

    void BuildLinePreview(const std::vector<cS3DVector3>& points,
                          const cS3DVector3& cursorPos,
                          bool cursorValid,
                          const std::vector<cS3DVector3>& plannedPositions);

    void BuildPolygonPreview(const std::vector<cS3DVector3>& vertices,
                             const cS3DVector3& cursorPos,
                             bool cursorValid,
                             const std::vector<cS3DVector3>& plannedPositions);

    void Draw(IDirect3DDevice7* device);

private:
    struct OverlayVertex {
        float x, y, z;
        DWORD color;
    };

    struct Layer {
        std::vector<OverlayVertex> vertices;
        bool visible = true;
    };

    struct SavedRenderState {
        DWORD zEnable = 0;
        DWORD zWriteEnable = 0;
        DWORD lighting = 0;
        DWORD alphaBlend = 0;
        DWORD cullMode = 0;
        DWORD zBias = 0;
        DWORD srcBlend = 0;
        DWORD dstBlend = 0;
        DWORD colorOp = 0;
        DWORD colorArg1 = 0;
        DWORD alphaOp = 0;
        DWORD alphaArg1 = 0;
    };

    void SetupRenderState_(IDirect3DDevice7* device);
    void RestoreRenderState_(IDirect3DDevice7* device);

    void EmitLine_(const cS3DVector3& a, const cS3DVector3& b, float thickness, DWORD color, uint32_t layer);
    void EmitQuad_(const cS3DVector3& a, const cS3DVector3& b, const cS3DVector3& c, const cS3DVector3& d,
                   DWORD color, uint32_t layer);
    void EmitMarker_(const cS3DVector3& center, float size, DWORD color, uint32_t layer);
    void EmitFilledPolygon_(const std::vector<cS3DVector3>& vertices, DWORD color, uint32_t layer);

private:
    static constexpr DWORD kLineColor = 0xC0FFFFFF;
    static constexpr DWORD kPolygonFillColor = 0x4000FF00;
    static constexpr DWORD kMarkerColor = 0xF0FFD700;
    static constexpr DWORD kPlannedMarkerColor = 0xF0FF8C00;
    static constexpr DWORD kCursorColor = 0xE0FF4444;
    static constexpr float kLineThickness = 0.6f;
    static constexpr float kMarkerSize = 1.0f;
    static constexpr float kHeightOffset = 0.18f;

    std::array<Layer, 2> layers_{};
    SavedRenderState savedState_{};
};
