#pragma once

#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include "cRZRect.h"
#include "TerrainDecalSymbols.hpp"

class SC4DrawContext;
class cISTETerrain;
class cISTETerrainView;

namespace TerrainDecal
{
    enum class OverlayUvMode : uint32_t
    {
        StretchSubrect = 0,
        ClipSubrect = 1,
    };

    enum class DrawResult
    {
        FallThroughToVanilla,
        // A handled draw must submit only the clipped decal geometry.
        // Outside the footprint, the already-rendered terrain must remain visible.
        Handled,
    };

    struct RendererOptions
    {
        bool enableClippedRendering = false;
    };

    struct DrawRequest
    {
        void* overlayManager = nullptr;
        SC4DrawContext* drawContext = nullptr;
        const cRZRect* rect = nullptr;
        const std::byte* overlaySlotBase = nullptr;
        const float* activeTexTransform = nullptr;
        int activeTexTransformStage = -1;
        std::ptrdiff_t overlayRectOffset = 0;
        const HookAddresses* addresses = nullptr;
        cISTETerrain* terrain = nullptr;
        cISTETerrainView* terrainView = nullptr;
    };

    struct OverlayUvSubrect
    {
        float u1 = 0.0f;
        float v1 = 0.0f;
        float u2 = 1.0f;
        float v2 = 1.0f;
        OverlayUvMode mode = OverlayUvMode::StretchSubrect;
    };

    class ClippedTerrainDecalRenderer final
    {
    public:
        explicit ClippedTerrainDecalRenderer(RendererOptions options = {});

        void SetOptions(const RendererOptions& options) noexcept;
        [[nodiscard]] const RendererOptions& GetOptions() const noexcept;

        void SetOverlayUvSubrect(uint32_t overlayId, const OverlayUvSubrect& uvRect);
        [[nodiscard]] bool RemoveOverlayUvSubrect(uint32_t overlayId) noexcept;
        void ClearOverlayUvSubrects() noexcept;
        [[nodiscard]] bool TryGetOverlayUvSubrect(uint32_t overlayId, OverlayUvSubrect& uvRect) const noexcept;

        [[nodiscard]] DrawResult Draw(const DrawRequest& request);

    private:
        RendererOptions options_;
        std::unordered_map<uint32_t, OverlayUvSubrect> overlayUvSubrects_;
    };
}
