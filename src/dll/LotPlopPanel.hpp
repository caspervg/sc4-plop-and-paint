#pragma once
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "FilterableTablePanel.hpp"
#include "LotFilterHelper.hpp"
#include "SC4AdvancedLotPlopDirector.hpp"
#include "public/ImGuiTexture.h"

constexpr auto kMaxIconsToLoadPerFrame = 50;

// UI Constants for consistent sizing across panels
namespace UI {
    constexpr auto kSearchBarWidth = 150.0f;
    constexpr auto kSliderWidth = 50.0f;
    constexpr auto kDropdownWidth = 100.0f;
    constexpr auto kIconColumnWidth = 45.0f;
    constexpr auto kIconSize = 44.0f;
    constexpr auto kNameColumnWidth = 75.0f;
    constexpr auto kSizeColumnWidth = 25.0f;
    constexpr auto kActionColumnWidth = 55.0f;
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
    void RenderTable_(const std::vector<LotView>& lotViews,
                      const std::unordered_set<uint32_t>& favorites);

    // Tab-specific rendering
    void RenderLotsTab_();
    void RenderFavButton_(uint32_t lotInstanceId) const;
    void RenderOccupantGroupFilter_();

    SC4AdvancedLotPlopDirector* director_;
    cIGZImGuiService* imguiService_;
    std::unordered_map<uint32_t, ImGuiTexture> iconCache_;
    uint32_t lastDeviceGeneration_ = 0;
    bool texturesLoaded_ = false;
    bool isOpen_ = false;
    std::unordered_set<uint32_t> openBuildings_;

    // Filter helper
    LotFilterHelper filterHelper_;
    std::vector<LotFilterHelper::SortSpec> sortSpecs_ = {
        {LotFilterHelper::SortColumn::Name, false}
    };
};
