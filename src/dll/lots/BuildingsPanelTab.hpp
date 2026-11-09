#pragma once
#include "../common/PanelTab.hpp"
#include "../pick/ScenePickResult.hpp"
#include "../thumbnail/ThumbnailCache.hpp"
#include "LotFilterHelper.hpp"
#include "BuildingStyleService.hpp"
#include "public/ImGuiTexture.h"

class BuildingsPanelTab : public PanelTab {
public:
    BuildingsPanelTab(SC4PlopAndPaintDirector* director,
                      LotRepository* lots, PropRepository* props, FavoritesRepository* favorites,
                      BuildingStyleService* buildingStyleService,
                      cIGZImGuiService* imguiService);

    ~BuildingsPanelTab() override = default;

    [[nodiscard]] const char* GetTabName() const override;
    void OnRender() override;
    void OnDeviceReset(uint32_t deviceGeneration) override;
    void OnShutdown() override {
        thumbnailCache_.Clear();
        previewCache_.Clear();
    }

private:
    ImGuiTexture LoadBuildingTexture_(uint64_t buildingKey);

    void RenderFilterUI_();
    // How many are shown, how many filters are on, and the way to clear them.
    void RenderResultLine_();
    void RenderBuildingsTable_(float tableHeight);
    void RenderLotsDetailTable_(float tableHeight);
    void RenderBuildingRow_(const Building& building, bool isSelected);
    void RenderLotRow_(const Lot& lot);
    void RenderOccupantGroupFilter_();
    void RenderBuildingStyleFilter_();
    void RenderFavButton_(uint32_t lotInstanceId) const;
    void HandlePickedLot_(const PickedLot& picked);

    // Filters and sorts the list again only when something it depends on has changed.
    void ApplyFilters_();
    void SortBuildings_();
    [[nodiscard]] const std::vector<BuildingStyleEntry>& GetBuildingStyles_() const;
    [[nodiscard]] const BuildingStyleContext& GetBuildingStyleContext_() const;

private:
    ThumbnailCache<uint64_t> thumbnailCache_;
    uint32_t lastDeviceGeneration_{0};

    const Building* selectedBuilding_ = nullptr;
    std::vector<const Building*> filteredBuildings_;

    LotFilterHelper filter_;
    // What the list was last made from.
    LotFilterHelper appliedFilter_;
    BuildingStyleContext appliedContext_;
    size_t appliedFavoriteCount_ = 0;
    size_t appliedBuildingCount_ = SIZE_MAX;
    BuildingStyleService* buildingStyleService_ = nullptr;
    // The cache was built before the building style data existed.
    bool cacheNeedsRebuild_ = false;

    // Sorting
    bool sortDescending_ = false;
};
