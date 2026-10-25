#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "PaintPlacement.hpp"
#include "cS3DVector3.h"
#include "public/SC4WorldMeshBuilder.h"
#include "public/cIGZWorldDrawService.h"

class cISTETerrain;
struct PropPaintSettings;

// Tool previews drawn into the city through cIGZWorldDrawService. Every Build/Add/Clear call
// hands the new geometry to the service's layers, which draw it every frame on any graphics
// driver until it changes again. Without a service (no SC4RenderServices world drawing, or
// EnableDrawOverlay=false) the overlay only keeps its geometry and draws nothing.
class PaintOverlay {
public:
    static constexpr uint32_t kLayerGrid = 0;
    static constexpr uint32_t kLayerShape = 1;
    static constexpr uint32_t kLayerMarkers = 2;

    struct PreviewPlacement {
        PlannedPaint placement{};
        bool valid = true;
        bool hasContinuousRotation = false;
        float continuousRotationRadians = 0.0f;
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

    PaintOverlay() = default;
    ~PaintOverlay();
    PaintOverlay(const PaintOverlay&) = delete;
    PaintOverlay& operator=(const PaintOverlay&) = delete;

    // Starts drawing through this service, or stops drawing with nullptr. Removes the layers
    // made on the previous service, so call it with nullptr before that service goes away.
    void SetWorldDrawService(cIGZWorldDrawService* service);

    void Clear();
    [[nodiscard]] bool Empty() const;

    void BuildStripperPreview(bool cursorValid, const cS3DVector3& cursorPos,
                              float pickRadius, cISTETerrain* terrain);
    void BuildStripperPreview(bool cursorValid, const cS3DVector3& cursorPos,
                              float pickRadius, cISTETerrain* terrain, uint32_t rectColor);

    // Appends a terrain-following XZ rectangle outline to the existing overlay
    // (does not Clear); used to highlight a picked lot texture's footprint.
    void AddRectOutline(float minX, float minZ, float maxX, float maxZ,
                        cISTETerrain* terrain, uint32_t color);

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

private:
    // A line that keeps its width on screen at every zoom level.
    struct ScreenLine {
        SC4WorldLineStyle style;
        std::vector<SC4WorldVertex> points;
    };

    struct Layer {
        SC4WorldMeshBuilder mesh;
        std::vector<ScreenLine> lines;
        uint32_t id = 0;
        bool dirty = false;
    };

    // Hands every changed layer to the world draw service.
    void Upload_();
    void DestroyLayers_();
    void ClearLayers_();

    void EmitLine_(const cS3DVector3& a, const cS3DVector3& b, float thickness, uint32_t color, uint32_t layer);
    void EmitScreenLine_(const std::vector<cS3DVector3>& points, float widthPixels, uint32_t color, bool closed,
                         uint32_t layer);
    void EmitQuad_(const cS3DVector3& a, const cS3DVector3& b, const cS3DVector3& c, const cS3DVector3& d,
                   uint32_t color, uint32_t layer);
    void EmitMarker_(const cS3DVector3& center, float size, uint32_t color, uint32_t layer);
    void EmitPreviewPlacement_(const PreviewPlacement& placement, cISTETerrain* terrain, uint32_t layer);
    void EmitFilledPolygon_(const std::vector<cS3DVector3>& vertices, uint32_t color, uint32_t layer);
    void EmitGrid_(const cS3DVector3& center, cISTETerrain* terrain, const PropPaintSettings& settings);

private:
    static constexpr uint32_t kGridMajorColor = 0x60FFFFFF;
    static constexpr uint32_t kGridMinorColor = 0x38B0E0FF;
    static constexpr uint32_t kLineColor = 0xC0FFFFFF;
    static constexpr uint32_t kPolygonFillColor = 0x4000FF00;
    static constexpr uint32_t kMarkerColor = 0xF0FFD700;
    static constexpr uint32_t kPlannedMarkerColor = 0xF0FF8C00;
    static constexpr uint32_t kPlannedBoxTopColor = 0x70FFB347;
    static constexpr uint32_t kPlannedBoxSideColor = 0x50FF8C00;
    static constexpr uint32_t kPlannedStiltColor = 0xA0FFD966;
    static constexpr uint32_t kInvalidMarkerColor = 0xF0FF5555;
    static constexpr uint32_t kInvalidBoxTopColor = 0x70FF6666;
    static constexpr uint32_t kInvalidBoxSideColor = 0x55CC3333;
    static constexpr uint32_t kInvalidStiltColor = 0xA0FF7777;
    static constexpr uint32_t kCursorColor = 0xE0FF4444;
    static constexpr float kLineThickness = 0.6f;
    static constexpr float kMarkerSize = 1.0f;
    static constexpr float kGridHeightExtraOffset = 0.05f;
    static constexpr float kHeightOffset = 0.15f;
    static constexpr float kStiltGapThreshold = 0.35f;

    // Screen-space widths, in pixels, of the lines that outline the tools.
    static constexpr float kGridMajorWidthPx = 2.5f;
    static constexpr float kGridMinorWidthPx = 1.5f;
    static constexpr float kPathWidthPx = 3.0f;
    static constexpr float kPolygonCloseWidthPx = 2.5f;
    static constexpr float kCursorCloseWidthPx = 1.5f;
    static constexpr float kPickRectWidthPx = 2.5f;
    static constexpr float kRectOutlineWidthPx = 2.0f;

    std::array<Layer, 3> layers_{};
    cIGZWorldDrawService* worldDraw_ = nullptr;
};
