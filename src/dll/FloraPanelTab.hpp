#pragma once
#include <string>
#include <vector>

#include "FloraRepository.hpp"
#include "PanelTab.hpp"
#include "PaintSettings.hpp"
#include "ThumbnailCache.hpp"
#include "public/ImGuiTexture.h"

class FloraPanelTab : public PanelTab {
public:
    FloraPanelTab(SC4PlopAndPaintDirector* director,
                  FloraRepository* flora,
                  FavoritesRepository* favorites,
                  cIGZImGuiService* imguiService);

    ~FloraPanelTab() override = default;

    [[nodiscard]] const char* GetTabName() const override;
    void OnRender() override;
    void OnShutdown() override { thumbnailCache_.Clear(); }
    void OnDeviceReset(uint32_t deviceGeneration) override;

private:
    ImGuiTexture LoadFloraTexture_(uint64_t key);

    void RenderFilterUI_();
    void RenderIndividualFloraTable_();
    void RenderGroupsSection_();
    void RenderPaintModal_();

    FloraRepository* flora_;
    ThumbnailCache<uint64_t> thumbnailCache_{};
    uint32_t lastDeviceGeneration_{0};

    char searchBuf_[256]{};
    bool favoritesOnly_{false};

    struct PendingPaintState {
        uint32_t typeId{0};
        std::string name;
        PropPaintSettings settings{};
        bool open{false};
        bool isGroup{false};
    };
    PendingPaintState pendingPaint_{};
};
