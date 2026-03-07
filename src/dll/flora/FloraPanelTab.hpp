#pragma once
#include <string>
#include <vector>

#include "../common/PanelTab.hpp"
#include "../paint/PaintSettings.hpp"
#include "../thumbnail/ThumbnailCache.hpp"
#include "FloraRepository.hpp"
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
    ImGuiTexture LoadFloraTexture_(uint64_t key) const;

    void RenderFilterUI_();
    void BuildFilteredFloraIndices_(std::vector<size_t>& filteredIndices) const;
    void RenderIndividualFloraTable_(const std::vector<size_t>& filteredIndices);
    [[nodiscard]] bool RenderFloraPills_(const Flora& flora, bool startOnNewLine) const;
    void RenderPaintModal_();
    void QueuePaintForFlora_(const Flora& flora);

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
    };
    PendingPaintState pendingPaint_{};
};
