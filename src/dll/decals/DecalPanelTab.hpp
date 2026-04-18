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
    void OnShutdown() override;

private:
    void RenderFilterBar_();
    void BuildFilteredIndices_(std::vector<size_t>& out) const;
    void RenderDecalGrid_(const std::vector<size_t>& indices);
    void RenderSettingsModal_();
    void RenderUvPicker_(TerrainDecalState& state);
    void QueuePaintForDecal_(uint32_t instanceId);
    void StartPaintingDecal_(uint32_t instanceId);
    void ClearUvPickerTexture_();
    bool EnsureUvPickerTextureLoaded_();
    bool LoadDecalTexture_(uint32_t instanceId, ImGuiTexture& outTexture, ImVec2& outSourceSize) const;
    ImGuiTexture LoadDecalThumbnail_(uint32_t instanceId) const;

    DecalRepository*             decals_;
    cIGZPersistResourceManager*  pRM_{nullptr};
    ThumbnailCache<uint32_t>     thumbnailCache_{};
    uint32_t                   lastDeviceGeneration_{0};

    ImGuiTexture uvPickerTexture_{};
    ImVec2       uvPickerSourceSize_{0.0f, 0.0f};
    uint32_t     uvPickerTextureInstanceId_{0};
    bool         uvPickerDragging_{false};
    bool         uvPickerHadWindowBeforeDrag_{false};
    TerrainDecalUvWindow uvPickerWindowBeforeDrag_{};
    ImVec2       uvPickerDragStartUv_{0.0f, 0.0f};

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
