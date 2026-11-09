#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "../common/PanelTab.hpp"
#include "../packs/PackFilter.hpp"
#include "../paint/DecalPaintSettings.hpp"
#include "../thumbnail/ThumbnailCache.hpp"
#include "DecalRepository.hpp"
#include "TextureHoverPreview.hpp"
#include "TextureKindFilter.hpp"
#include "public/ImGuiTexture.h"

class cIGZPersistResourceManager;

class DecalPanelTab : public PanelTab {
public:
    DecalPanelTab(SC4PlopAndPaintDirector* director,
                  DecalRepository* decals,
                  FavoritesRepository* favorites,
                  cIGZPersistResourceManager* pRM,
                  cIGZImGuiService* imguiService);

    [[nodiscard]] const char* GetTabName() const override { return "Terrain Decals"; }
    void OnRender() override;
    void OnDeviceReset(uint32_t deviceGeneration) override;
    void OnShutdown() override;

    // Terrain Paint hands these over when it changes to and from this mode.
    void SetSearch(const char* text, int32_t pack);
    [[nodiscard]] const char* SearchText() const { return searchBuf_; }
    [[nodiscard]] int32_t SelectedPack() const { return packFilter_.Selected(); }

private:
    void IndexPacks_();
    void RenderFilterBar_();
    void RenderResultLine_();
    // Stop painting, pick and strip.
    void RenderTools_();
    // What can be done with the selected texture.
    void RenderSelectionLine_();
    void ClearFilters_();
    // Filters again only when something it depends on has changed.
    void RefreshShown_();
    void BuildFilteredIndices_(std::vector<size_t>& out) const;
    void RenderDecalGrid_();
    void RenderSettingsModal_();
    void RenderUvSection_(TerrainDecalState& state);
    void RenderPresets_(const TerrainDecalState& state);
    void RenderFaAngleCombo_(float& rotationDegrees, TerrainDecalState& state);
    std::string CurrentFaAngleLabel_(float rotationDegrees) const;
    void RenderUvPicker_(TerrainDecalState& state);
    void QueuePaintForDecal_(uint32_t instanceId);
    void StartPaintingDecal_(uint32_t instanceId);
    // Takes over what was painted with last, as R, F, M and the depth keys change that.
    void UseLastPaintState_(uint32_t instanceId);
    void ApplyFavoritePresetIfAvailable_(uint32_t instanceId);
    void LoadFavoritePresetByName_(uint32_t instanceId, const char* presetName);
    void ResetPresetNameInput_();
    void ClearUvPickerTexture_();
    bool EnsureUvPickerTextureLoaded_();
    bool LoadDecalTexture_(uint32_t instanceId, ImGuiTexture& outTexture, ImVec2& outSourceSize) const;
    ImGuiTexture LoadDecalThumbnail_(uint32_t instanceId) const;

    DecalRepository*             decals_;
    cIGZPersistResourceManager*  pRM_{nullptr};
    ThumbnailCache<uint32_t>     thumbnailCache_{};
    TextureHoverPreview          hoverPreview_{};
    uint32_t                   lastDeviceGeneration_{0};

    ImGuiTexture uvPickerTexture_{};
    ImVec2       uvPickerSourceSize_{0.0f, 0.0f};
    uint32_t     uvPickerTextureInstanceId_{0};
    bool         uvPickerDragging_{false};
    bool         uvPickerHadWindowBeforeDrag_{false};
    TerrainDecalUvWindow uvPickerWindowBeforeDrag_{};
    ImVec2       uvPickerDragStartUv_{0.0f, 0.0f};

    // Filter state
    char    searchBuf_[64]{};
    bool    favoritesOnly_{false};
    int     wealthFilter_{0};
    int     dilapidationFilter_{0};
    TextureKindFilter kindFilter_{};
    PackFilter packFilter_{};
    size_t     indexedPackCount_{SIZE_MAX};
    // By index into the instance IDs.
    std::vector<std::string> searchTexts_{};
    TextSearch::Suggester suggester_{};

    // The textures shown, and what they were filtered with.
    std::vector<size_t> shown_{};
    bool        shownDirty_{true};
    std::string appliedSearch_{};
    bool        appliedFavoritesOnly_{false};
    size_t      appliedFavoriteCount_{0};
    int         appliedWealth_{0};
    int         appliedDilapidation_{0};
    bool        appliedBase_{true};
    bool        appliedOverlay_{true};
    size_t      appliedTotal_{0};
    int32_t     appliedPack_{PackFilter::kAllPacks};

    uint32_t selectedInstanceId_{0};
    uint32_t scrollToInstanceId_{0};
    char     presetNameBuf_[64]{};
    int      selectedFavoritePresetIndex_{-1};

    struct PendingPaintState {
        uint32_t instanceId{0};
        DecalPaintSettings settings{};
        bool open{false};
    };
    PendingPaintState pendingPaint_{};
    // The dialog edits this copy, so that Cancel changes nothing.
    TerrainDecalState dialogState_{};
};
