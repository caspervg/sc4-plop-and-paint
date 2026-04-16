#pragma once

#include <memory>
#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "ClippedTerrainDecalRenderer.hpp"
#include "TerrainDecalSymbols.hpp"
#include "X86RelativeCallPatch.hpp"

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
            bool logInterceptedDraws = false;
        };

    public:
        explicit TerrainDecalHook(Options options = {});
        ~TerrainDecalHook();

        [[nodiscard]] bool Install();
        void Uninstall();

        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] std::string_view GetLastError() const noexcept;
        [[nodiscard]] const std::optional<HookAddresses>& GetAddresses() const noexcept;

        [[nodiscard]] ClippedTerrainDecalRenderer& GetRenderer() noexcept;
        [[nodiscard]] const ClippedTerrainDecalRenderer& GetRenderer() const noexcept;

    private:
#if defined(_M_IX86)
        using DrawRectFn = void(__thiscall*)(void*, SC4DrawContext*, const cRZRect*);
        using SetTexTransform4Fn = void(__thiscall*)(SC4DrawContext*, void*, int);
        static void __fastcall DrawRectCallThunk(void* overlayManager, void*, SC4DrawContext* drawContext, const cRZRect* rect);
        static void __fastcall SetTexTransform4CallThunk(SC4DrawContext* drawContext, void*, void* transform4x4, int stage);
#else
        using DrawRectFn = void(*)(void*, SC4DrawContext*, const cRZRect*);
        static void DrawRectCallThunk(void* overlayManager, SC4DrawContext* drawContext, const cRZRect* rect);
#endif

        void HandleDrawRectCall_(void* overlayManager, SC4DrawContext* drawContext, const cRZRect* rect);
        void HandleSetTexTransform4Call_(SC4DrawContext* drawContext, void* transform4x4, int stage);
        void CallOriginalDrawRect_(void* overlayManager, SC4DrawContext* drawContext, const cRZRect* rect) const;
        void CallOriginalSetTexTransform4_(SC4DrawContext* drawContext, void* transform4x4, int stage) const;
        void SetLastError_(std::string message);

    private:
        Options options_;
        std::optional<HookAddresses> addresses_;
        X86RelativeCallPatch callSitePatch_;
        X86RelativeCallPatch setTexTransform4CallSitePatch_;
        ClippedTerrainDecalRenderer renderer_;
        std::string lastError_;
        std::array<float, 16> lastStage0TexTransform_{};
        SC4DrawContext* lastStage0TexTransformContext_ = nullptr;
        bool hasLastStage0TexTransform_ = false;

        static TerrainDecalHook* sActiveHook_;
    };
}

