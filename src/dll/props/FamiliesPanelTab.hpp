#pragma once

#include <string>

#include "../common/PanelTab.hpp"
#include "../packs/PackFilter.hpp"
#include "../thumbnail/ThumbnailCache.hpp"
#include "PropPainterInputControl.hpp"

class FamiliesPanelTab : public PanelTab {
public:
    FamiliesPanelTab(SC4PlopAndPaintDirector* director,
                     LotRepository* lots, PropRepository* props, FavoritesRepository* favorites,
                     cIGZImGuiService* imguiService)
        : PanelTab(director, lots, props, favorites, imguiService) {
        familyPaintDefaults_.showGrid = director->GetDefaultShowGridOverlay();
        familyPaintDefaults_.snapPointsToGrid = director->GetDefaultSnapPointsToGrid();
        familyPaintDefaults_.snapPlacementsToGrid = director->GetDefaultSnapPlacementsToGrid();
        familyPaintDefaults_.gridStepMeters = director->GetDefaultGridStepMeters();
        familyPaintDefaults_.previewMode = director->GetDefaultPropPreviewMode();
        pendingPaint_.settings.showGrid = director->GetDefaultShowGridOverlay();
        pendingPaint_.settings.snapPointsToGrid = director->GetDefaultSnapPointsToGrid();
        pendingPaint_.settings.snapPlacementsToGrid = director->GetDefaultSnapPlacementsToGrid();
        pendingPaint_.settings.gridStepMeters = director->GetDefaultGridStepMeters();
        pendingPaint_.settings.previewMode = director->GetDefaultPropPreviewMode();
    }

    [[nodiscard]] const char* GetTabName() const override;
    void OnRender() override;
    void OnDeviceReset(uint32_t deviceGeneration) override;
    void OnShutdown() override {
        thumbnailCache_.Clear();
        previewCache_.Clear();
    }

private:
    ImGuiTexture LoadPropTexture_(uint64_t propKey) const;
    struct FamilyRow {
        size_t combinedIndex = 0;
        bool isAuto = false;
        const PropFamily* family = nullptr;
        std::optional<uint32_t> familyId{};
        uint64_t favoriteKey = 0;
    };

    void IndexPacks_();
    void RenderFilters_();
    [[nodiscard]] std::vector<FamilyRow> BuildRows_() const;
    void RenderResultLine_(size_t shown, size_t total);
    void RenderTools_();
    void RenderFamilyTable_(const std::vector<FamilyRow>& rows, float height);
    // The selected family: its name, what can be done with it, and its props.
    void RenderDetails_();
    void RenderMemberTable_(const PropFamily& family, bool isAuto, size_t userIndex, float height);
    void RenderMemberGrid_(const PropFamily& family, bool isAuto, size_t userIndex, float height);
    void RenderPropStar_(const Prop& prop) const;
    void ClearFilters_();
    void OpenNewFamily_();
    void RenderNewFamilyPopup_();
    void QueuePaintForSelectedFamily_();
    void RenderPaintOptionsPopup_();
    bool StartPaintingWithSelectedFamily_();

    // Returns the active PropFamily* given the combined index (auto first, then user).
    [[nodiscard]] const PropFamily* GetSelectedFamily_() const;
    [[nodiscard]] bool IsSelectedAutoFamily_() const;

private:
    ThumbnailCache<uint64_t> thumbnailCache_{};
    uint32_t lastDeviceGeneration_{0};
    size_t selectedFamilyIndex_{0};
    bool newFamilyPopupOpen_ = false;
    char newFamilyName_[128] = {};
    std::string nameFilter_{};
    std::string iidFilter_{};
    PackFilter packFilter_{};
    // By combined family index, for packFilter_.
    std::vector<std::vector<PackCatalogRepository::ExemplarRow>> familyPacks_{};
    size_t indexedPackSignature_{SIZE_MAX};
    // By combined family index.
    std::vector<std::string> familyTexts_{};
    TextSearch::Suggester suggester_{};
    bool favoritesOnly_ = false;
    // The own family whose name is being typed, and what has been typed so far.
    size_t renameFamilyIndex_ = SIZE_MAX;
    std::string renameFamilyName_{};
    PropPaintSettings familyPaintDefaults_{};

    struct PendingFamilyPaintState {
        uint32_t fallbackPropId = 0;
        RecentPaintEntry::SourceKind sourceKind{RecentPaintEntry::SourceKind::PropAutoFamily};
        uint64_t sourceId = 0;
        std::string familyName;
        PropPaintSettings settings{};
        bool open = false;
    };

    PendingFamilyPaintState pendingPaint_{};
};
