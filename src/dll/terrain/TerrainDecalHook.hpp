#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "ClippedTerrainDecalRenderer.hpp"
#include "TerrainDecalSymbols.hpp"
#include "RelativeCallPatch.hpp"

class SC4DrawContext;

namespace TerrainDecal
{
    class TerrainDecalHook final
    {
    public:
        struct Options
        {
            bool installEnabled = true;
            bool enableExperimentalRenderer = false;
        };

    public:
        explicit TerrainDecalHook(Options options = {});
        ~TerrainDecalHook();

        [[nodiscard]] bool Install();
        void Uninstall();

        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] std::string_view GetLastError() const noexcept;
        [[nodiscard]] const std::optional<HookAddresses>& GetAddresses() const noexcept;

        void SetOverlayUvSubrect(uint32_t overlayId, const OverlayUvSubrect& uvRect);
        [[nodiscard]] bool RemoveOverlayUvSubrect(uint32_t overlayId) noexcept;
        void ClearOverlayUvSubrects() noexcept;
        [[nodiscard]] bool TryGetOverlayUvSubrect(uint32_t overlayId, OverlayUvSubrect& uvRect) const noexcept;

        [[nodiscard]] ClippedTerrainDecalRenderer& GetRenderer() noexcept;
        [[nodiscard]] const ClippedTerrainDecalRenderer& GetRenderer() const noexcept;

    private:
        using DrawRectFn = void(__thiscall*)(void*, SC4DrawContext*, const cRZRect*);
        using SetTexTransform4Fn = void(__thiscall*)(SC4DrawContext*, const float*, int);
        static void __fastcall DrawRectCallThunk(void* overlayManager, void*, SC4DrawContext* drawContext, const cRZRect* rect);
        static void __fastcall SetTexTransform4CallThunk(SC4DrawContext* drawContext, void*, const float* matrix, int stage);

        void HandleDrawRectCall_(void* overlayManager, SC4DrawContext* drawContext, const cRZRect* rect);
        void HandleSetTexTransform4Call_(SC4DrawContext* drawContext, const float* matrix, int stage);
        void CallOriginalDrawRect_(void* overlayManager, SC4DrawContext* drawContext, const cRZRect* rect) const;
        void CallOriginalSetTexTransform4_(SC4DrawContext* drawContext, const float* matrix, int stage) const;
        void SetLastError_(std::string message);

    private:
        Options options_;
        std::optional<HookAddresses> addresses_;
        RelativeCallPatch callSitePatch_;
        RelativeCallPatch setTexTransformCallSitePatch_;
        ClippedTerrainDecalRenderer renderer_;
        std::string lastError_;
        std::array<float, 16> currentTexTransform_{};
        int currentTexTransformStage_ = -1;
        bool currentTexTransformValid_ = false;

        static TerrainDecalHook* sActiveHook_;
    };
}
