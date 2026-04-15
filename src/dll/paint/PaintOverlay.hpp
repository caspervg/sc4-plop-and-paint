#pragma once
#define WIN32_LEAN_AND_MEAN

#include <array>
#include <cstdint>
#include <vector>

#include <d3d.h>

#include "PaintPlacement.hpp"
#include "cS3DVector3.h"

class cISTETerrain;
struct PropPaintSettings;

class PaintOverlay {
public:
    static constexpr uint32_t kLayerGrid = 0;
    static constexpr uint32_t kLayerShape = 1;
    static constexpr uint32_t kLayerMarkers = 2;

    struct PreviewPlacement {
        PlannedPaint placement{};
        bool valid = true;
        float width = 0.0f;
        float height = 0.0f;
        float depth = 0.0f;
        float minX = 0.0f;
        float maxX = 0.0f;
        float minY = 0.0f;
        float maxY = 0.0f;
        float minZ = 0.0f;
        float maxZ = 0.0f;
    };

    void Clear();
    [[nodiscard]] bool Empty() const;

    void BuildStripperPreview(bool cursorValid, const cS3DVector3& cursorPos,
                              float pickRadius, cISTETerrain* terrain);

    void BuildDirectPreview(bool cursorValid,
                            const cS3DVector3& cursorPos,
                            cISTETerrain* terrain,
                            const PropPaintSettings& settings,
                            const PreviewPlacement& plannedPlacement,
                            bool drawPlacement = true);

    void BuildLinePreview(const std::vector<cS3DVector3>& points,
                          const std::vector<cS3DVector3>& terrainAnchors,
                          const cS3DVector3& cursorPos,
                          const cS3DVector3& cursorTerrainPos,
                          bool cursorValid,
                          cISTETerrain* terrain,
                          const PropPaintSettings& settings,
                          const std::vector<PreviewPlacement>& plannedPlacements);

    void BuildPolygonPreview(const std::vector<cS3DVector3>& vertices,
                             const std::vector<cS3DVector3>& terrainAnchors,
                             const cS3DVector3& cursorPos,
                             const cS3DVector3& cursorTerrainPos,
                             bool cursorValid,
                             cISTETerrain* terrain,
                             const PropPaintSettings& settings,
                             const std::vector<PreviewPlacement>& plannedPlacements);

    void Draw(IDirect3DDevice7* device, bool drawGrid = true);

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
        DWORD alphaTest = 0;
        DWORD cullMode = 0;
        DWORD fogEnable = 0;
        DWORD zBias = 0;
        DWORD srcBlend = 0;
        DWORD dstBlend = 0;
        DWORD colorOp = 0;
        DWORD colorArg1 = 0;
        DWORD colorArg2 = 0;
        DWORD alphaOp = 0;
        DWORD alphaArg1 = 0;
        DWORD alphaArg2 = 0;
        DWORD stage1ColorOp = 0;
        DWORD stage1AlphaOp = 0;
        IDirectDrawSurface7* texture0 = nullptr;
    };

    void SetupRenderState_(IDirect3DDevice7* device);
    void RestoreRenderState_(IDirect3DDevice7* device);

    void EmitLine_(const cS3DVector3& a, const cS3DVector3& b, float thickness, DWORD color, uint32_t layer);
    void EmitQuad_(const cS3DVector3& a, const cS3DVector3& b, const cS3DVector3& c, const cS3DVector3& d,
                   DWORD color, uint32_t layer);
    void EmitMarker_(const cS3DVector3& center, float size, DWORD color, uint32_t layer);
    void EmitPreviewPlacement_(const PreviewPlacement& placement, cISTETerrain* terrain, uint32_t layer);
    void EmitFilledPolygon_(const std::vector<cS3DVector3>& vertices, DWORD color, uint32_t layer);
    void EmitGrid_(const cS3DVector3& center, cISTETerrain* terrain, const PropPaintSettings& settings);

private:
    static constexpr DWORD kGridMajorColor = 0x60FFFFFF;
    static constexpr DWORD kGridMinorColor = 0x38B0E0FF;
    static constexpr DWORD kLineColor = 0xC0FFFFFF;
    static constexpr DWORD kPolygonFillColor = 0x4000FF00;
    static constexpr DWORD kMarkerColor = 0xF0FFD700;
    static constexpr DWORD kPlannedMarkerColor = 0xF0FF8C00;
    static constexpr DWORD kPlannedBoxTopColor = 0x70FFB347;
    static constexpr DWORD kPlannedBoxSideColor = 0x50FF8C00;
    static constexpr DWORD kPlannedStiltColor = 0xA0FFD966;
    static constexpr DWORD kInvalidMarkerColor = 0xF0FF5555;
    static constexpr DWORD kInvalidBoxTopColor = 0x70FF6666;
    static constexpr DWORD kInvalidBoxSideColor = 0x55CC3333;
    static constexpr DWORD kInvalidStiltColor = 0xA0FF7777;
    static constexpr DWORD kCursorColor = 0xE0FF4444;
    static constexpr float kLineThickness = 0.6f;
    static constexpr float kMarkerSize = 1.0f;
    static constexpr float kGridHeightExtraOffset = 0.05f;
    static constexpr float kHeightOffset = 0.15f;
    static constexpr float kStiltGapThreshold = 0.35f;

    std::array<Layer, 3> layers_{};
    SavedRenderState savedState_{};
};
