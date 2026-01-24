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
    constexpr auto kSearchBarWidth = 200.0f;
    constexpr auto kSliderWidth = 75.0f;
    constexpr auto kDropdownWidth = 150.0f;
    constexpr auto kFavColumnWidth = 30.0f;
    constexpr auto kIconColumnWidth = 50.0f;
    constexpr auto kIconSize = 44.0f;
    constexpr auto kNameColumnWidth = 300.0f;
    constexpr auto kSizeColumnWidth = 50.0f;
    constexpr auto kPlopColumnWidth = 40.0f;
    constexpr auto kTableHeight = 400.0f;
}

class LotPlopPanel final : public FilterableTablePanel, public ImGuiPanel {     
public:
    explicit LotPlopPanel(SC4AdvancedLotPlopDirector* director, cIGZImGuiService* imguiService);
    void OnInit() override {}
    void OnRender() override;
    void OnShutdown() override { delete this; }
    void SetOpen(bool open);
    [[nodiscard]] bool IsOpen() const;

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
    bool isOpen_ = false;

    // Filter helper
    LotFilterHelper filterHelper_;
};
