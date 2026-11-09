#pragma once
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

#include "../common/PanelTab.hpp"
#include "../thumbnail/ThumbnailCache.hpp"
#include "../thumbnail/ThumbnailStore.hpp"
#include "../../shared/networks.hpp"

// The network items of the Network Addon Mod, in two views.
//
// Drag: a tile for every override network, under the menu button its starter piece is in. A click starts
// the game's drag tool for the base network, and the NAM DLL turns each drag into the override network,
// with no starter piece.
//
// Pieces: every puzzle piece of the game's network menus. The menu buttons are on the left, in the order
// of the menus, and the pieces that TAB goes through are on the right. A click starts the game's own
// tool on that piece.
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
    using PieceRef = std::pair<size_t, size_t>;  // button, piece in it

    void RenderStatus_(bool canDrag);
    void RenderDragView_(bool canDrag);
    void RenderPiecesView_();
    void RenderButtonList_(const std::vector<size_t>& buttons, const std::vector<size_t>& matches);
    void RenderNetworkTile_(const OverrideNetwork& network, bool active);
    void RenderPieceTile_(PieceRef ref, bool active);
    // The picture, the name on a band and an optional badge, on the tile drawn last.
    void RenderTileFace_(uint64_t key, const std::string& name, const char* badge, bool active);
    [[nodiscard]] bool PieceMatches_(const NetworkPieceButton& button, const NetworkPiece& piece) const;
    [[nodiscard]] ImGuiTexture LoadTexture_(uint64_t key, float tileSize);

    NetworksCache cache_;
    std::vector<Group> groups_;
    std::vector<std::string> menus_;  // the menus that have buttons, in order
    ThumbnailStore thumbnails_;
    ThumbnailCache<uint64_t> thumbnailCache_{};
    ImGuiTextFilter filter_;
    int menu_ = -1;                 // index into menus_, -1 for all
    static constexpr size_t kRecent = static_cast<size_t>(-1);
    size_t selectedButton_ = 0;     // index into cache_.buttons, or kRecent
    std::vector<PieceRef> recent_;  // the pieces used last, newest first
    size_t visibleTiles_ = 0;
    uint32_t lastDeviceGeneration_ = 0;
};
