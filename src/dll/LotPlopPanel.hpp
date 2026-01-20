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
    constexpr float kSearchBarWidth = 300.0f;
    constexpr float kSliderWidth = 100.0f;
    constexpr float kDropdownWidth = 200.0f;
    constexpr float kStarColumnWidth = 30.0f;
    constexpr float kIconColumnWidth = 40.0f;
    constexpr float kTableHeight = 400.0f;
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
    void RenderStarButton_(const Lot& lot);
    void RenderOccupantGroupFilter_();

    SC4AdvancedLotPlopDirector* director_;
    cIGZImGuiService* imguiService_;
    std::unordered_map<uint32_t, ImGuiTexture> iconCache_;
    uint32_t lastDeviceGeneration_ = 0;
    bool texturesLoaded_ = false;

    // Filter helper
    LotFilterHelper filterHelper_;
};
