#pragma once
#include <array>
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
// the order of the menus under a heading for each menu that opens and closes, and the pieces that TAB
// goes through are on the right. A search or a network filter shows the matches of all buttons at once, and a click on a
// button then goes to its pieces. Tiles and buttons show the short labels of the cache builder
// (PieceLabels); the search also goes by the full names.
//
// The base networks and the buttons of the list show the icons of their buttons in the game's menus.
//
// The Network Eraser has a button of its own above both views.
// A click starts the game's own tool on that piece, and the tab follows when TAB, Home or End in the
// game change the piece.
//
// Home and End mostly turn a piece, but some rows of a TAB loop hold more pieces, such as the left and
// the right switch. A tile shows one of them, with a badge that lists the others. The tile shows the one
// that the search found, or the one being placed.
class NetworksPanelTab final : public PanelTab {
public:
    NetworksPanelTab(SC4PlopAndPaintDirector* director, FavoritesRepository* favorites,
                     cIGZImGuiService* imguiService);

    [[nodiscard]] const char* GetTabName() const override { return "Networks"; }
    void OnRender() override;
    void OnDeviceReset(uint32_t deviceGeneration) override;
    void OnShutdown() override {
        thumbnailCache_.Clear();
        iconCache_.Clear();
        previewCache_.Clear();
    }

private:
    enum class View { Drag, Pieces };
    struct Group {
        std::string name;
        std::vector<size_t> networks;  // indices into cache_.networks
    };
    using PieceRef = std::pair<size_t, size_t>;  // button, piece in it
    // A piece as a tile shows it: variant 0 is the piece, n is its variants[n - 1].
    struct Tile {
        PieceRef ref;
        size_t variant = 0;
        bool operator==(const Tile&) const = default;
    };
    // A piece or one of its variants, made ready to show and search once.
    struct Variant {
        std::string name;
        std::string shortName;   // without the words that all pieces of the button share
        std::string pickerName;  // without the words that all variants of the piece share
        uint32_t id = 0;
        uint32_t cost = 0;
        uint32_t networks = 0;   // the networks in its cells, bit n is eNetworkType n
        std::string size;        // in tiles, empty for one tile
        std::string search;      // TextSearch::Normalize of its name and where it is
        std::string nameSearch;  // the same, with its own name only
    };

    void RenderFilterUI_();
    void RenderResultLine_(size_t shown, size_t total, const char* items);
    void RenderToolRow_();
    void RenderNoResults_(const char* text);
    void RenderDragView_(const TextSearch::Query& query);
    void RenderPiecesView_(const TextSearch::Query& query);
    // collapsible: the menu headings open and close. Without, as while searching, all matches show.
    void RenderButtonList_(const std::vector<size_t>& buttons, const std::vector<std::vector<size_t>>& pieces,
                           size_t recentCount, bool collapsible);
    void RenderEraserButton_();
    // A base network's button: the icon of its menu button, then its name. A plain button without an icon.
    bool RenderBaseNetworkButton_(const char* name, uint32_t icon, bool active);
    [[nodiscard]] float BaseNetworkButtonWidth_(const char* name, uint32_t icon) const;
    // The texture of an Item Icon, once it has loaded. Asks for it to load.
    std::optional<void*> Icon_(uint32_t icon);
    // Icons are as high as a row of the button list.
    [[nodiscard]] static float IconSide_() { return ImGui::GetTextLineHeight() * 2.0f; }
    void RenderNetworkTile_(const OverrideNetwork& network, bool active);
    // The tiles of these pieces. shortNames: all are of one button, which the view shows above them.
    // The tiles from brightCount on are greyed out: the rest of a TAB loop around the pieces that match.
    void RenderPieceGrid_(const std::vector<Tile>& tiles, std::optional<Tile> active, bool shortNames,
                          size_t brightCount = static_cast<size_t>(-1));
    void RenderPieceTile_(Tile tile, bool active, bool shortName);
    // The other pieces of a tile's row, to pick one. active: the variant being placed, if it is of this piece.
    void RenderVariantPicker_(PieceRef ref, std::optional<size_t> active);
    // Starts the game's tool on a piece and remembers it in Recent.
    void PlacePiece_(Tile tile);
    // The picture, the name on a band, an optional badge and the star, on the tile drawn last. A
    // favoriteKey of 0 has no star and no menu. Returns the top of the band.
    float RenderTileFace_(uint64_t key, const std::string& name, const char* badge, bool active, uint64_t favoriteKey);
    void RenderTileMenu_(uint64_t favoriteKey) const;
    void ClearFilters_();
    [[nodiscard]] int ActiveFilterCount_() const;
    [[nodiscard]] bool IsFavorite_(uint64_t favoriteKey) const;
    [[nodiscard]] bool VariantFiltered_(const Variant& variant) const;
    [[nodiscard]] bool PieceShown_(PieceRef ref, const TextSearch::Query& query) const;
    // Favorites only and the network filter, which hide a piece whatever the search.
    [[nodiscard]] bool PieceFiltered_(PieceRef ref) const;
    // Whether the search finds the piece by its own name or that of one of its variants.
    [[nodiscard]] bool PieceNamed_(PieceRef ref, const TextSearch::Query& query) const;
    // The variant that a tile of the piece shows: the first that the search finds by name, else the first
    // that the search and the filters let through.
    [[nodiscard]] Tile TileOf_(PieceRef ref, const TextSearch::Query& query) const;
    [[nodiscard]] std::optional<Tile> ActiveTile_() const;
    [[nodiscard]] std::string ActiveTool_() const;
    [[nodiscard]] std::string NetworkFilterLabel_() const;
    [[nodiscard]] float TileSize_() const { return ShownThumbnailSize_(); }
    [[nodiscard]] ImGuiTexture LoadTexture_(uint64_t key, float tileSize);

    NetworksCache cache_;
    std::vector<Group> groups_;
    std::vector<std::string> menus_;  // the menus that have buttons, in order
    // The buttons of the button list, indices into cache_.buttons in the order of the game's menus.
    // Without the Network Eraser, which has a button of its own.
    std::vector<size_t> buttonOrder_;
    size_t eraserButton_ = kNone;     // index into cache_.buttons of the Network Eraser, kNone without one
    size_t pieceCount_ = 0;
    // TextSearch::Normalize of each item, made once.
    std::vector<std::string> baseSearch_;                // by kBaseNetworks
    std::vector<std::string> networkSearch_;             // by cache_.networks
    // By cache_.buttons, then piece, then variant: 0 is the piece itself.
    std::vector<std::vector<std::vector<Variant>>> variants_;
    std::unordered_map<uint32_t, Tile> pieceById_;  // every RUL0 ID that TAB, Home and End reach
    std::vector<uint32_t> networkChoices_;  // the networks that some piece has, in the order of the menus
    TextSearch::Suggester suggester_;
    ThumbnailStore thumbnails_;
    ThumbnailCache<uint64_t> thumbnailCache_{};
    ThumbnailCache<uint64_t> iconCache_{};  // at IconSide_(), apart from the tiles, which are larger
    std::array<uint32_t, 13> baseIcons_{};  // Item Icon of each base network, by eNetworkType, 0 for none
    bool buttonIcons_ = false;              // some button of the list has an icon: the list makes room for it

    View view_ = View::Drag;
    char searchBuf_[256] = {};
    bool favoritesOnly_ = false;
    int menu_ = -1;                 // index into menus_, -1 for all
    uint32_t networkFilter_ = 0;    // the networks a piece must all have, bit n is eNetworkType n
    static constexpr size_t kRecent = static_cast<size_t>(-1);
    size_t selectedButton_ = 0;     // index into cache_.buttons, or kRecent
    std::vector<Tile> recent_;      // the pieces used last, newest first
    uint32_t lastActivePiece_ = 0;
    std::optional<PieceRef> reveal_;  // a piece to scroll to, once it is drawn
    bool openSelectedMenu_ = true;    // open the menu heading of the selected button in the list, once
    size_t scrollToButton_ = kNone;   // a button whose pieces to scroll to, in the view of all matches
    static constexpr size_t kNone = static_cast<size_t>(-2);
    size_t visibleTiles_ = 0;
    size_t visibleIcons_ = 0;
    uint32_t lastDeviceGeneration_ = 0;
};
