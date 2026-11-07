#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>

#include "../common/PanelTab.hpp"
#include "../decals/DecalPanelTab.hpp"
#include "../decals/DecalRepository.hpp"
#include "../thumbnail/ThumbnailCache.hpp"
#include "GroundToolSettings.hpp"

class cIGZPersistResourceManager;

// The Ground tab: paint areas, lines and brush strokes on the terrain with textures or colours,
// rub them out, and edit, reorder and hide what was painted. The Stamp tool is the single-texture
// decal painter that this tab grew out of.
class GroundPanelTab : public PanelTab {
public:
    GroundPanelTab(SC4PlopAndPaintDirector* director,
                   DecalRepository* decals,
                   FavoritesRepository* favorites,
                   cIGZPersistResourceManager* pRM,
                   cIGZImGuiService* imguiService);

    [[nodiscard]] const char* GetTabName() const override { return "Terrain Paint"; }
    void OnRender() override;
    void OnDeviceReset(uint32_t deviceGeneration) override;
    void OnShutdown() override;

private:
    void RenderToolRow_();
    // What to do next with the tool in use, from the city's input control.
    void RenderStatus_();
    // Opacity, soft edge and wear, folded away by default.
    void RenderFinish_(Ground::Look& look, float& wear, bool& changed, bool& committed);
    void RenderGrid_();
    // What the look editor remembers between frames: Texture chosen while no texture is picked
    // yet, and the plain colour while a texture is in use. The look's colour is the paint colour
    // with Colour and the tint with Texture; switching keeps one from turning into the other.
    struct LookEditorState {
        bool wantsTexture = false;
        std::array<float, 3> plainColour{1.0f, 1.0f, 1.0f};
    };

    // followsLine: the shape is a line, whose texture can follow it.
    void RenderLookEditor_(Ground::Look& look, bool followsLine, LookEditorState& state, bool& changed, bool& committed);
    void RenderChosenTextures_(Ground::Look& look, bool& changed, bool& committed);
    void RenderTextureLibrary_(Ground::Look& look, bool& committed);
    // A texture tile: its thumbnail, outlined when in use. True when clicked.
    bool TextureTile_(uint32_t instanceId, float size, bool inUse);
    void UseTexture_(Ground::Look& look, uint32_t instanceId, bool addToMix, bool& committed);
    // The library's textures for the pack, search and favourites, worked out again only when they
    // change.
    const std::vector<uint32_t>& ShownTextures_();
    // The library by pack, original file and ID, the search text of each, and how many each pack
    // has: made once per library.
    void IndexLibrary_();
    void RenderPackFilter_();
    void TextureTooltip_(uint32_t instanceId) const;
    void RenderShapeSettings_();
    void RenderSelection_(Ground::GroundPaintDocument& document);
    void RenderSelectionActions_(Ground::GroundPaintDocument& document);
    void RenderTransform_(Ground::GroundPaintDocument& document);
    void RenderLayers_(Ground::GroundPaintDocument& document);
    // Marks the tool settings changed; FlushSettings_ hands them to the city's tool, at most every
    // 150 ms while a control is held.
    void SettingsChanged_();
    void FlushSettings_();
    // Shape settings of the tool in use, without the grid.
    void SelectTool_(Ground::Tool tool);

    DecalRepository* decals_;
    cIGZPersistResourceManager* pRM_;
    std::unique_ptr<DecalPanelTab> stampTab_;
    ThumbnailCache<uint32_t> thumbnails_{};

    Ground::ToolSettings settings_{};
    bool settingsDirty_ = false;
    std::chrono::steady_clock::time_point lastSettingsPush_{};
    bool stamping_ = false;

    // The selected object being edited, applied when a control is let go.
    uint32_t editingId_ = 0;
    Ground::PaintObject editing_{};

    // Turning and scaling the selection: shown while a control is dragged, done when let go.
    float turnDegrees_ = 0.0f;
    float scalePercent_ = 100.0f;
    Ground::Vec2 transformPivot_{};

    char textureFilter_[64]{};
    bool favouriteTexturesOnly_ = false;
    std::vector<uint32_t> shownTextures_{};
    std::string shownFilter_{};
    bool shownFavouritesOnly_ = false;
    size_t shownFavouriteCount_ = 0;
    int32_t shownPack_ = 0;
    bool shownValid_ = false;
    // kAllPacks, kNoPack (not in the catalog), or an index into the catalog's packs.
    static constexpr int32_t kAllPacks = -2;
    static constexpr int32_t kNoPack = -1;
    int32_t packFilter_ = kAllPacks;
    struct LibraryEntry {
        uint32_t id = 0;
        // Every pack that ships it; none when the catalog does not know it.
        std::vector<int32_t> packs{};
        // The first pack's name and file, to sort by.
        const std::string* sortPack = nullptr;
        const std::string* sortFile = nullptr;
        std::string searchText{};  // ID, original files and packs, in capitals
    };
    std::vector<LibraryEntry> library_{};
    size_t indexedLibrarySize_ = SIZE_MAX;
    // Installed textures per pack, by pack index; the last entry counts those not in the catalog.
    std::vector<uint32_t> packCounts_{};
    std::vector<int32_t> packsByName_{};

    // Newest first, this session.
    std::vector<uint32_t> recentTextures_{};
    LookEditorState toolLook_{};
    uint32_t lookPickCount_ = 0;
    LookEditorState selectionLook_{};
};
