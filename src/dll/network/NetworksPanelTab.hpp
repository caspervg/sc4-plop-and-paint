#pragma once
#include <optional>
#include <string>
#include <unordered_map>
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
// the order of the menus, and the pieces that TAB goes through are on the right. A search or a network
// filter shows the matches of all buttons at once, and a click on a button then goes to its pieces.
// A click starts the game's own tool on that piece, and the tab follows when TAB, Home or End in the
// game change the piece.
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
    // The tiles of these pieces. shortNames: all are of one button, which the view shows above them.
    // The tiles from brightCount on are greyed out: the rest of a TAB loop around the pieces that match.
    void RenderPieceGrid_(const std::vector<PieceRef>& refs, uint32_t activePiece, bool shortNames,
                          size_t brightCount = static_cast<size_t>(-1));
    void RenderPieceTile_(PieceRef ref, bool active, bool shortName);
    // The picture, the name on a band, an optional badge and the star, on the tile drawn last.
    void RenderTileFace_(uint64_t key, const std::string& name, const char* badge, bool active, uint64_t favoriteKey);
    void RenderTileMenu_(uint64_t favoriteKey) const;
    void ClearFilters_();
    [[nodiscard]] int ActiveFilterCount_() const;
    [[nodiscard]] bool IsFavorite_(uint64_t favoriteKey) const;
    [[nodiscard]] bool PieceShown_(PieceRef ref, const TextSearch::Query& query) const;
    // Favorites only and the network filter, which hide a piece whatever the search.
    [[nodiscard]] bool PieceFiltered_(PieceRef ref) const;
    [[nodiscard]] std::string ActiveTool_() const;
    [[nodiscard]] std::string NetworkFilterLabel_() const;
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
    std::vector<std::vector<std::string>> pieceNameSearch_;  // the same, with the piece's own name only
    // Also by cache_.buttons, then piece: the name without the words that all pieces of the button
    // share, the networks in its cells (bit n is eNetworkType n) and its size in tiles, empty for one tile.
    std::vector<std::vector<std::string>> pieceShortName_;
    std::vector<std::vector<uint32_t>> pieceNetworks_;
    std::vector<std::vector<std::string>> pieceSize_;
    std::unordered_map<uint32_t, PieceRef> pieceById_;
    std::vector<uint32_t> networkChoices_;  // the networks that some piece has, in the order of the menus
    TextSearch::Suggester suggester_;
    ThumbnailStore thumbnails_;
    ThumbnailCache<uint64_t> thumbnailCache_{};

    View view_ = View::Drag;
    char searchBuf_[256] = {};
    bool favoritesOnly_ = false;
    int menu_ = -1;                 // index into menus_, -1 for all
    uint32_t networkFilter_ = 0;    // the networks a piece must all have, bit n is eNetworkType n
    static constexpr size_t kRecent = static_cast<size_t>(-1);
    size_t selectedButton_ = 0;     // index into cache_.buttons, or kRecent
    std::vector<PieceRef> recent_;  // the pieces used last, newest first
    uint32_t lastActivePiece_ = 0;
    std::optional<PieceRef> reveal_;  // a piece to scroll to, once it is drawn
    size_t scrollToButton_ = kNone;   // a button whose pieces to scroll to, in the view of all matches
    static constexpr size_t kNone = static_cast<size_t>(-2);
    size_t visibleTiles_ = 0;
    uint32_t lastDeviceGeneration_ = 0;
};
