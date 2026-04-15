#pragma once

#include <algorithm>

#include "../thumbnail/ThumbnailCache.hpp"
#include "public/ImGuiPanel.h"
#include "public/ImGuiTexture.h"

class SC4PlopAndPaintDirector;
class PropRepository;
class FloraRepository;
class cIGZImGuiService;

class RecentSwapPanel final : public ImGuiPanel {
public:
    void OnInit() override {}
    void OnRender() override;
    void OnShutdown() override {
        thumbnailCache_.Clear();
        visible_ = false;
        lastDeviceGeneration_ = 0;
    }

    void SetVisible(const bool visible) { visible_ = visible; }
    [[nodiscard]] bool IsVisible() const { return visible_; }

    void SetDirector(SC4PlopAndPaintDirector* director) { director_ = director; }
    void SetRepositories(PropRepository* props, FloraRepository* flora, cIGZImGuiService* imguiService) {
        props_ = props;
        flora_ = flora;
        imguiService_ = imguiService;
    }

private:
    void RenderTooltip_(size_t index, bool isCurrent, bool isFlora);
    [[nodiscard]] ImGuiTexture LoadThumbnail_(uint64_t key);
    static void ReleaseImGuiInputCapture_();

    SC4PlopAndPaintDirector* director_ = nullptr;
    PropRepository* props_ = nullptr;
    FloraRepository* flora_ = nullptr;
    cIGZImGuiService* imguiService_ = nullptr;
    bool visible_ = false;
    uint32_t lastDeviceGeneration_{0};
    ThumbnailCache<uint64_t> thumbnailCache_{24, 8};
};
