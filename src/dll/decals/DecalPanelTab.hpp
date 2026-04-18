#pragma once
#include <cstdint>
#include <vector>

#include "../common/PanelTab.hpp"
#include "../paint/DecalPaintSettings.hpp"
#include "../thumbnail/ThumbnailCache.hpp"
#include "DecalRepository.hpp"
#include "public/ImGuiTexture.h"

class cIGZPersistResourceManager;

class DecalPanelTab : public PanelTab {
public:
    DecalPanelTab(SC4PlopAndPaintDirector* director,
                  DecalRepository* decals,
                  cIGZPersistResourceManager* pRM,
                  cIGZImGuiService* imguiService);

    [[nodiscard]] const char* GetTabName() const override { return "Terrain Decals"; }
    void OnRender() override;
    void OnDeviceReset(uint32_t deviceGeneration) override;
    void OnShutdown() override { thumbnailCache_.Clear(); }

private:
    void RenderFilterBar_();
    void BuildFilteredIndices_(std::vector<size_t>& out) const;
    void RenderDecalGrid_(const std::vector<size_t>& indices);
    void RenderSettingsModal_();
    void QueuePaintForDecal_(uint32_t instanceId);
    void StartPaintingDecal_(uint32_t instanceId);
    ImGuiTexture LoadDecalThumbnail_(uint32_t instanceId) const;

    DecalRepository*             decals_;
    cIGZPersistResourceManager*  pRM_{nullptr};
    ThumbnailCache<uint32_t>     thumbnailCache_{};
    uint32_t                   lastDeviceGeneration_{0};

    // Filter state
    char    iidFilterBuf_[17]{};

    uint32_t selectedInstanceId_{0};

    struct PendingPaintState {
        uint32_t instanceId{0};
        DecalPaintSettings settings{};
        bool open{false};
    };
    PendingPaintState pendingPaint_{};
};
