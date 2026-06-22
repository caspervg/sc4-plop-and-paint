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
    void OnShutdown() override { thumbnailCache_.Clear(); }

private:
    ImGuiTexture LoadBuildingTexture_(uint64_t buildingKey);

    void RenderFilterUI_();
    void RenderBuildingsTable_(float tableHeight);
    void RenderLotsDetailTable_(float tableHeight);
    void RenderBuildingRow_(const Building& building, bool isSelected);
    void RenderLotRow_(const Lot& lot);
    void RenderOccupantGroupFilter_();
    void RenderBuildingStyleFilter_();
    void RenderFavButton_(uint32_t lotInstanceId) const;
    void HandlePickedLot_(const PickedLot& picked);

    void ApplyFilters_();
    void InitializeFallbackBuildingStyles_();
    [[nodiscard]] const std::vector<BuildingStyleEntry>& GetBuildingStyles_() const;
    [[nodiscard]] const BuildingStyleContext& GetBuildingStyleContext_() const;

private:
    ThumbnailCache<uint64_t> thumbnailCache_;
    uint32_t lastDeviceGeneration_{0};

    const Building* selectedBuilding_ = nullptr;
    std::vector<const Building*> filteredBuildings_;

    LotFilterHelper filter_;
    BuildingStyleService* buildingStyleService_ = nullptr;
    std::vector<BuildingStyleEntry> fallbackBuildingStyles_;
    BuildingStyleContext fallbackBuildingStyleContext_;

    // Sorting
    bool sortDescending_ = false;
};
