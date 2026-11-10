#pragma once
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

#include "../common/PanelTab.hpp"
#include "../common/TextSearch.hpp"
#include "../thumbnail/ThumbnailCache.hpp"
#include "../thumbnail/ThumbnailStore.hpp"
#include "../../shared/networks.hpp"

// The network items of the Network Addon Mod, in two views that share the search and filters.
//
// Drag networks: the game's own drag tools, then a tile for every override network, under the menu
// button its starter piece is in. A click starts the game's drag tool for the base network, and the
// NAM DLL turns each drag into the override network, with no starter piece.
//
// Place pieces: every puzzle piece of the game's network menus. The menu buttons are on the left, in
// the order of the menus, and the pieces that TAB goes through are on the right. A click starts the
// game's own tool on that piece.
class NetworksPanelTab final : public PanelTab {
public:
    NetworksPanelTab(SC4PlopAndPaintDirector* director, FavoritesRepository* favorites,
                     cIGZImGuiService* imguiService);

    [[nodiscard]] const char* GetTabName() const override { return "Networks"; }
    void OnRender() override;
    void OnDeviceReset(uint32_t deviceGeneration) override;
    void OnShutdown() override {
        thumbnailCache_.Clear();
        previewCache_.Clear();
    }

private:
    enum class View { Drag, Pieces };
    struct Group {
        std::string name;
        std::vector<size_t> networks;  // indices into cache_.networks
    };
    using PieceRef = std::pair<size_t, size_t>;  // button, piece in it

    void RenderFilterUI_();
    void RenderResultLine_(size_t shown, size_t total, const char* items);
    void RenderToolRow_();
    void RenderNoResults_(const char* text);
    void RenderDragView_(const TextSearch::Query& query);
    void RenderPiecesView_(const TextSearch::Query& query);
    void RenderButtonList_(const std::vector<size_t>& buttons, const std::vector<std::vector<size_t>>& pieces,
                           size_t recentCount);
    void RenderNetworkTile_(const OverrideNetwork& network, bool active);
    void RenderPieceTile_(PieceRef ref, bool active);
    // The picture, the name on a band, an optional badge and the star, on the tile drawn last.
    void RenderTileFace_(uint64_t key, const std::string& name, const char* badge, bool active, uint64_t favoriteKey);
    void RenderTileMenu_(uint64_t favoriteKey) const;
    void ClearFilters_();
    [[nodiscard]] int ActiveFilterCount_() const;
    [[nodiscard]] bool IsFavorite_(uint64_t favoriteKey) const;
    [[nodiscard]] bool PieceShown_(PieceRef ref, const TextSearch::Query& query) const;
    [[nodiscard]] std::string ActiveTool_() const;
    [[nodiscard]] float TileSize_() const { return ShownThumbnailSize_(); }
    [[nodiscard]] ImGuiTexture LoadTexture_(uint64_t key, float tileSize);

    NetworksCache cache_;
    std::vector<Group> groups_;
    std::vector<std::string> menus_;  // the menus that have buttons, in order
    size_t pieceCount_ = 0;
    // TextSearch::Normalize of each item, made once.
    std::vector<std::string> baseSearch_;                // by kBaseNetworks
    std::vector<std::string> networkSearch_;             // by cache_.networks
    std::vector<std::vector<std::string>> pieceSearch_;  // by cache_.buttons, then piece
    TextSearch::Suggester suggester_;
    ThumbnailStore thumbnails_;
    ThumbnailCache<uint64_t> thumbnailCache_{};

    View view_ = View::Drag;
    char searchBuf_[256] = {};
    bool favoritesOnly_ = false;
    int menu_ = -1;                 // index into menus_, -1 for all
    static constexpr size_t kRecent = static_cast<size_t>(-1);
    size_t selectedButton_ = 0;     // index into cache_.buttons, or kRecent
    std::vector<PieceRef> recent_;  // the pieces used last, newest first
    size_t visibleTiles_ = 0;
    uint32_t lastDeviceGeneration_ = 0;
};
