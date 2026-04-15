#pragma once

#include <cstddef>
#include <vector>

#include "TerrainDecalExamples.hpp"
#include "TerrainDecalSymbols.hpp"
#include "cRZRect.h"

class SC4DrawContext;
class cISTETerrain;
class cISTETerrainView;

namespace TerrainDecal
{
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
        bool logInterceptedDraws = false;
    };

    struct DrawRequest
    {
        void* overlayManager = nullptr;
        SC4DrawContext* drawContext = nullptr;
        const cRZRect* rect = nullptr;
        const std::byte* overlaySlotBase = nullptr;
        std::ptrdiff_t overlayRectOffset = 0;
        const HookAddresses* addresses = nullptr;
        cISTETerrain* terrain = nullptr;
        cISTETerrainView* terrainView = nullptr;
    };

    class ClippedTerrainDecalRenderer final
    {
    public:
        explicit ClippedTerrainDecalRenderer(RendererOptions options = {});

        void SetOptions(const RendererOptions& options) noexcept;
        [[nodiscard]] const RendererOptions& GetOptions() const noexcept;

        [[nodiscard]] DrawResult Draw(const DrawRequest& request);
        [[nodiscard]] DrawResult DrawCommands(const DrawRequest& request,
                                             const std::vector<ClippedDecalDrawCommand>& commands);

    private:
        RendererOptions options_;
        bool loggedNotImplemented_ = false;
    };
}
