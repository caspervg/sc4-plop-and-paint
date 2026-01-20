#pragma once
#include <unordered_map>
#include <vector>
#include "SC4AdvancedLotPlopDirector.hpp"
#include "public/ImGuiTexture.h"
#include "FilterableTablePanel.hpp"
#include "LotFilterHelper.hpp"

constexpr auto kMaxIconsToLoadPerFrame = 50;

// UI Constants for consistent sizing across panels
namespace UI {
    constexpr auto kSearchBarWidth = 300.0f;
    constexpr auto kSliderWidth = 100.0f;
    constexpr auto kDropdownWidth = 200.0f;
    constexpr auto kStarColumnWidth = 30.0f;
    constexpr auto kIconColumnWidth = 40.0f;
    constexpr auto kLotNameColumnWidth = 250.0f;
    constexpr auto kSizeColumnWidth = 60.0f;
    constexpr auto kGrowthColumnWidth = 60.0f;
    constexpr auto kBuildingNameColumnWidth = 250.0f;
    constexpr auto kPlopColumnWidth = 60.0f;
    constexpr auto kTableHeight = 400.0f;
}

class LotPlopPanel final : public FilterableTablePanel, public ImGuiPanel {
public:
    explicit LotPlopPanel(SC4AdvancedLotPlopDirector* director, cIGZImGuiService* imguiService);
    void OnInit() override {}
    void OnRender() override;
    void OnShutdown() override { delete this; }

private:
    void LoadIconTexture_(uint32_t buildingInstanceId, const Building& building);

    // FilterableTablePanel interface
    void RenderFilterUI_() override;
    void RenderTable_() override;

    // Internal rendering with filtered data
    void RenderTable_(const std::vector<const Lot*>& filteredLots);

    // Tab-specific rendering
    void RenderLotsTab_();
    void RenderFavButton_(const Lot& lot) const;
    void RenderOccupantGroupFilter_();

    SC4AdvancedLotPlopDirector* director_;
    cIGZImGuiService* imguiService_;
    std::unordered_map<uint32_t, ImGuiTexture> iconCache_;
    uint32_t lastDeviceGeneration_ = 0;
    bool texturesLoaded_ = false;

    // Filter helper
    LotFilterHelper filterHelper_;
};
