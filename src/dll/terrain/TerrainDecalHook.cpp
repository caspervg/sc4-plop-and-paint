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

    TerrainDecalHook::TerrainDecalHook(const Options options)
        : options_(options)
        , renderer_(RendererOptions{
              .enableClippedRendering = options.enableExperimentalRenderer,
          })
    {
    }

    TerrainDecalHook::~TerrainDecalHook()
    {
        Uninstall();
    }

    bool TerrainDecalHook::Install()
    {
        if (callSitePatch_.IsInstalled() && setTexTransformCallSitePatch_.IsInstalled()) {
            return true;
        }

        if (!options_.installEnabled) {
            SetLastError_("terrain decal hook disabled by configuration");
            return false;
        }

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
        setTexTransformCallSitePatch_.Configure("cSTEOverlayManager::DrawDecals->SetTexTransform4 call site",
                                                addresses_->setTexTransform4CallSite,
                                                reinterpret_cast<void*>(&SetTexTransform4CallThunk));

        if (!callSitePatch_.Install()) {
            sActiveHook_ = nullptr;
            SetLastError_("failed to install draw-rect call-site patch");
            return false;
        }
        if (!setTexTransformCallSitePatch_.Install()) {
            callSitePatch_.Uninstall();
            sActiveHook_ = nullptr;
            SetLastError_("failed to install set-tex-transform call-site patch");
            return false;
        }

        lastError_.clear();
        LOG_INFO("TerrainDecalHook: installed at 0x{:08X} / 0x{:08X} for {}",
                 static_cast<uint32_t>(addresses_->drawRectCallSite),
                 static_cast<uint32_t>(addresses_->setTexTransform4CallSite),
                 DescribeKnownAddressSet(addresses_->gameVersion));
        return true;
    }

    void TerrainDecalHook::Uninstall()
    {
        setTexTransformCallSitePatch_.Uninstall();
        callSitePatch_.Uninstall();
        currentTexTransformValid_ = false;
        currentTexTransformStage_ = -1;
        renderer_.ClearOverlayUvSubrects();

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

    void TerrainDecalHook::SetOverlayUvSubrect(const uint32_t overlayId, const OverlayUvSubrect& uvRect)
    {
        renderer_.SetOverlayUvSubrect(overlayId, uvRect);
    }

    bool TerrainDecalHook::RemoveOverlayUvSubrect(const uint32_t overlayId) noexcept
    {
        return renderer_.RemoveOverlayUvSubrect(overlayId);
    }

    void TerrainDecalHook::ClearOverlayUvSubrects() noexcept
    {
        renderer_.ClearOverlayUvSubrects();
    }

    bool TerrainDecalHook::TryGetOverlayUvSubrect(const uint32_t overlayId, OverlayUvSubrect& uvRect) const noexcept
    {
        return renderer_.TryGetOverlayUvSubrect(overlayId, uvRect);
    }

    ClippedTerrainDecalRenderer& TerrainDecalHook::GetRenderer() noexcept
    {
        return renderer_;
    }

    const ClippedTerrainDecalRenderer& TerrainDecalHook::GetRenderer() const noexcept
    {
        return renderer_;
    }

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
                                                                const float* matrix,
                                                                int stage)
    {
        if (!sActiveHook_) {
            return;
        }

        sActiveHook_->HandleSetTexTransform4Call_(drawContext, matrix, stage);
    }

    void TerrainDecalHook::HandleDrawRectCall_(void* overlayManager, SC4DrawContext* drawContext, const cRZRect* rect)
    {
        DrawRequest request{
            .overlayManager = overlayManager,
            .drawContext = drawContext,
            .rect = rect,
            .overlaySlotBase = nullptr,
            .activeTexTransform = currentTexTransformValid_ ? currentTexTransform_.data() : nullptr,
            .activeTexTransformStage = currentTexTransformValid_ ? currentTexTransformStage_ : -1,
            .overlayRectOffset = 0,
            .addresses = addresses_ ? &*addresses_ : nullptr,
            .terrain = nullptr,
            .terrainView = nullptr,
        };

        if (addresses_ && rect && addresses_->overlayRectOffset > 0) {
            request.overlayRectOffset = addresses_->overlayRectOffset;
            request.overlaySlotBase = reinterpret_cast<const std::byte*>(rect) - addresses_->overlayRectOffset;
        }

        const cISC4AppPtr app;
        cISC4City* city = app ? app->GetCity() : nullptr;
        request.terrain = city ? city->GetTerrain() : nullptr;
        request.terrainView = request.terrain ? request.terrain->GetView() : nullptr;

        const auto result = renderer_.Draw(request);
        currentTexTransformValid_ = false;
        currentTexTransformStage_ = -1;

        if (result == DrawResult::Handled) {
            return;
        }

        CallOriginalDrawRect_(overlayManager, drawContext, rect);
    }

    void TerrainDecalHook::HandleSetTexTransform4Call_(SC4DrawContext* drawContext, const float* matrix, const int stage)
    {
        if (matrix) {
            std::copy_n(matrix, currentTexTransform_.size(), currentTexTransform_.begin());
            currentTexTransformStage_ = stage;
            currentTexTransformValid_ = true;
        }
        else {
            currentTexTransformValid_ = false;
            currentTexTransformStage_ = -1;
        }

        CallOriginalSetTexTransform4_(drawContext, matrix, stage);
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
                                                         const float* matrix,
                                                         const int stage) const
    {
        const auto originalTarget = setTexTransformCallSitePatch_.GetOriginalTarget();
        if (!originalTarget) {
            return;
        }

        const auto original = reinterpret_cast<SetTexTransform4Fn>(originalTarget);
        original(drawContext, matrix, stage);
    }

    void TerrainDecalHook::SetLastError_(std::string message)
    {
        lastError_ = std::move(message);
    }
}
