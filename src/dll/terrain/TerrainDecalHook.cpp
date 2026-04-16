#include "TerrainDecalHook.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "GZServPtrs.h"
#include "cISC4App.h"
#include "cISC4City.h"
#include "cISTETerrain.h"
#include "cISTETerrainView.h"
#include "utils/Logger.h"
#include "utils/VersionDetection.h"

namespace TerrainDecal
{
    TerrainDecalHook* TerrainDecalHook::sActiveHook_ = nullptr;
    std::optional<bool> lolz = std::nullopt;

    TerrainDecalHook::TerrainDecalHook(const Options options)
        : options_(options)
        , renderer_(RendererOptions{
              .enableClippedRendering = options.enableExperimentalRenderer,
              .logInterceptedDraws = options.logInterceptedDraws,
          })
    {
    }

    TerrainDecalHook::~TerrainDecalHook()
    {
        Uninstall();
    }

    bool TerrainDecalHook::Install()
    {
        if (callSitePatch_.IsInstalled()) {
            return true;
        }

        if (!options_.installEnabled) {
            SetLastError_("terrain decal hook disabled by configuration");
            return false;
        }

#if !defined(_M_IX86)
        SetLastError_("terrain decal hook skeleton is only implemented for Windows x86");
        LOG_WARN("TerrainDecalHook: {}", lastError_);
        return false;
#else
        const auto gameVersion = VersionDetection::GetInstance().GetGameVersion();
        addresses_ = ResolveHookAddresses(gameVersion);
        if (!addresses_) {
            SetLastError_(std::string("unsupported game version: ") + std::to_string(gameVersion));
            LOG_INFO("TerrainDecalHook: {}. No patch installed.", lastError_);
            return false;
        }

        if (sActiveHook_ && sActiveHook_ != this) {
            SetLastError_("another terrain decal hook instance is already active");
            LOG_WARN("TerrainDecalHook: {}", lastError_);
            return false;
        }

        sActiveHook_ = this;
        callSitePatch_.Configure("cSTEOverlayManager::DrawDecals->DrawRect call site",
                                 addresses_->drawRectCallSite,
                                 reinterpret_cast<void*>(&DrawRectCallThunk));
        setTexTransform4CallSitePatch_.Configure("cSTEOverlayManager::DrawDecals->SetTexTransform4 call site",
                                                 addresses_->drawDecalsSetTexTransform4CallSite,
                                                 reinterpret_cast<void*>(&SetTexTransform4CallThunk));

        if (!callSitePatch_.Install()) {
            sActiveHook_ = nullptr;
            SetLastError_("failed to install draw-rect call-site patch");
            return false;
        }
        if (!setTexTransform4CallSitePatch_.Install()) {
            callSitePatch_.Uninstall();
            sActiveHook_ = nullptr;
            SetLastError_("failed to install SetTexTransform4 call-site patch");
            return false;
        }

        lastError_.clear();
        LOG_INFO("TerrainDecalHook: installed skeleton at 0x{:08X} for {}",
                 static_cast<uint32_t>(addresses_->drawRectCallSite),
                 DescribeKnownAddressSet(addresses_->gameVersion));
        return true;
#endif
    }

    void TerrainDecalHook::Uninstall()
    {
        callSitePatch_.Uninstall();
        setTexTransform4CallSitePatch_.Uninstall();

        if (sActiveHook_ == this) {
            sActiveHook_ = nullptr;
        }
    }

    bool TerrainDecalHook::IsInstalled() const noexcept
    {
        return callSitePatch_.IsInstalled();
    }

    std::string_view TerrainDecalHook::GetLastError() const noexcept
    {
        return lastError_;
    }

    const std::optional<HookAddresses>& TerrainDecalHook::GetAddresses() const noexcept
    {
        return addresses_;
    }

    ClippedTerrainDecalRenderer& TerrainDecalHook::GetRenderer() noexcept
    {
        return renderer_;
    }

    const ClippedTerrainDecalRenderer& TerrainDecalHook::GetRenderer() const noexcept
    {
        return renderer_;
    }

#if defined(_M_IX86)
    void __fastcall TerrainDecalHook::DrawRectCallThunk(void* overlayManager,
                                                        void*,
                                                        SC4DrawContext* drawContext,
                                                        const cRZRect* rect)
    {
        if (!sActiveHook_) {
            return;
        }

        sActiveHook_->HandleDrawRectCall_(overlayManager, drawContext, rect);
    }

    void __fastcall TerrainDecalHook::SetTexTransform4CallThunk(SC4DrawContext* drawContext,
                                                                void*,
                                                                void* transform4x4,
                                                                int stage)
    {
        if (!sActiveHook_) {
            return;
        }

        sActiveHook_->HandleSetTexTransform4Call_(drawContext, transform4x4, stage);
    }
#else
    void TerrainDecalHook::DrawRectCallThunk(void* overlayManager, SC4DrawContext* drawContext, const cRZRect* rect)
    {
        if (!sActiveHook_) {
            return;
        }

        sActiveHook_->HandleDrawRectCall_(overlayManager, drawContext, rect);
    }
#endif

    void TerrainDecalHook::HandleDrawRectCall_(void* overlayManager, SC4DrawContext* drawContext, const cRZRect* rect)
    {
        DrawRequest request{
            .overlayManager = overlayManager,
            .drawContext = drawContext,
            .rect = rect,
            .overlaySlotBase = nullptr,
            .overlayRectOffset = 0,
            .addresses = addresses_ ? &*addresses_ : nullptr,
            .terrain = nullptr,
            .terrainView = nullptr,
            .effectiveTexTransform = nullptr,
        };

        if (addresses_ && rect && addresses_->overlayRectOffset > 0) {
            request.overlayRectOffset = addresses_->overlayRectOffset;
            request.overlaySlotBase = reinterpret_cast<const std::byte*>(rect) - addresses_->overlayRectOffset;
        }

        const cISC4AppPtr app;
        cISC4City* city = app ? app->GetCity() : nullptr;
        request.terrain = city ? city->GetTerrain() : nullptr;
        request.terrainView = request.terrain ? request.terrain->GetView() : nullptr;
        if (hasLastStage0TexTransform_ && lastStage0TexTransformContext_ == drawContext) {
            request.effectiveTexTransform = lastStage0TexTransform_.data();
        }

        const auto result = renderer_.Draw(request);
        if (options_.logInterceptedDraws && rect) {
            const char* outcome = result == DrawResult::Handled ? "custom renderer" : "vanilla renderer";
            LOG_INFO("TerrainDecalHook: draw rect x={} y={} w={} h={} used {}",
                     rect->nX,
                     rect->nY,
                     rect->nWidth,
                     rect->nHeight,
                     outcome);
        }

        if (result == DrawResult::Handled) {
            return;
        }

        CallOriginalDrawRect_(overlayManager, drawContext, rect);
    }

    void TerrainDecalHook::HandleSetTexTransform4Call_(SC4DrawContext* drawContext, void* transform4x4, int stage)
    {
        if (stage == 0 && drawContext && transform4x4) {
            const auto* const source = reinterpret_cast<const float*>(transform4x4);
            std::copy_n(source, lastStage0TexTransform_.size(), lastStage0TexTransform_.begin());
            lastStage0TexTransformContext_ = drawContext;
            hasLastStage0TexTransform_ = true;
        }

        CallOriginalSetTexTransform4_(drawContext, transform4x4, stage);
    }

    void TerrainDecalHook::CallOriginalDrawRect_(void* overlayManager,
                                                 SC4DrawContext* drawContext,
                                                 const cRZRect* rect) const
    {
        const auto originalTarget = callSitePatch_.GetOriginalTarget();
        if (!originalTarget) {
            return;
        }

        const auto original = reinterpret_cast<DrawRectFn>(originalTarget);
        original(overlayManager, drawContext, rect);
    }

    void TerrainDecalHook::CallOriginalSetTexTransform4_(SC4DrawContext* drawContext,
                                                         void* transform4x4,
                                                         int stage) const
    {
        const auto originalTarget = setTexTransform4CallSitePatch_.GetOriginalTarget();
        if (!originalTarget) {
            return;
        }

        const auto original = reinterpret_cast<SetTexTransform4Fn>(originalTarget);
        original(drawContext, transform4x4, stage);
    }

    void TerrainDecalHook::SetLastError_(std::string message)
    {
        lastError_ = std::move(message);
    }
}
