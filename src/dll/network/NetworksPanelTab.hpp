#pragma once
#include <string>
#include <vector>

#include "imgui.h"

#include "../common/PanelTab.hpp"
#include "../thumbnail/ThumbnailCache.hpp"
#include "../thumbnail/ThumbnailStore.hpp"
#include "../../shared/networks.hpp"

// A tile for every override network of the Network Addon Mod, under the menu button its starter piece
// is in. A click starts the game's drag tool for the base network, and the NAM DLL turns each drag into
// the override network, with no starter piece.
class NetworksPanelTab final : public PanelTab {
public:
    NetworksPanelTab(SC4PlopAndPaintDirector* director, cIGZImGuiService* imguiService);

    [[nodiscard]] const char* GetTabName() const override { return "Networks"; }
    void OnRender() override;
    void OnDeviceReset(uint32_t deviceGeneration) override;
    void OnShutdown() override { thumbnailCache_.Clear(); }

private:
    struct Group {
        std::string name;
        std::vector<size_t> networks;  // indices into cache_.networks
    };

    void RenderTile_(const OverrideNetwork& network, bool active);
    [[nodiscard]] ImGuiTexture LoadTexture_(uint64_t key, float tileSize);

    NetworksCache cache_;
    std::vector<Group> groups_;
    ThumbnailStore thumbnails_;
    ThumbnailCache<uint64_t> thumbnailCache_{};
    ImGuiTextFilter filter_;
    uint32_t lastDeviceGeneration_ = 0;
};
