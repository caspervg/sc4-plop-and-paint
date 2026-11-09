#include "BuildingsPanelTab.hpp"

#include <algorithm>
#include <format>

#include "OccupantGroups.hpp"
#include "../common/BadgeUtils.hpp"
#include "../common/Utils.hpp"
#include "../utils/Logger.h"

using PanelUi::CheckboxWidth;
using PanelUi::Hint;
using PanelUi::kGroupGap;
using PanelUi::NextInRow;
using PanelUi::Tooltip;

namespace {
    std::string CollapseConsecutiveNewlines(const std::string& text) {
        std::string result;
        result.reserve(text.size());

        bool lastWasNewline = false;
        for (const char ch : text) {
            if (ch == '\n') {
                if (!lastWasNewline) {
                    result.push_back(ch);
                }
                lastWasNewline = true;
            }
            else {
                result.push_back(ch);
                lastWasNewline = false;
            }
        }

        return result;
    }
}

BuildingsPanelTab::BuildingsPanelTab(
    SC4PlopAndPaintDirector* director,
    LotRepository* lots,
    PropRepository* props,
    FavoritesRepository* favorites,
    BuildingStyleService* buildingStyleService,
    cIGZImGuiService* imguiService)
    : PanelTab(director, lots, props, favorites, imguiService),
      buildingStyleService_(buildingStyleService) {
    const auto& buildings = lots_->GetBuildings();
    cacheNeedsRebuild_ = !buildings.empty() && std::ranges::none_of(buildings, [](const Building& building) {
        return building.purposeType.has_value();
    });
}

const char* BuildingsPanelTab::GetTabName() const {
    return "Buildings & Lots";
}

void BuildingsPanelTab::OnRender() {
    const auto& buildings = lots_->GetBuildings();

    if (buildings.empty()) {
        ImGui::TextWrapped("No buildings loaded. Run the cache builder to make lots.cbor in the Plugins folder, "
                           "then restart the game.");
        return;
    }

    RenderFilterUI_();
    ImGui::Separator();

    ApplyFilters_();
    RenderResultLine_();
    if (filteredBuildings_.empty()) {
        if (PanelUi::NoResults("No buildings match these filters.")) {
            filter_.ResetFilters();
        }
        else if (const auto suggestion = suggester_.Suggest(filter_.searchBuffer);
                 suggestion && PanelUi::DidYouMean(*suggestion)) {
            filter_.searchBuffer = *suggestion;
        }
        return;
    }

    // The building list takes what the lots do not need. Both stay usable in a short panel,
    // which then scrolls.
    const float line = ImGui::GetTextLineHeightWithSpacing();
    const float available = ImGui::GetContentRegionAvail().y;
    const float minimumBuildings = UI::iconRowHeight() * 3.0f + line;
    if (!selectedBuilding_) {
        RenderBuildingsTable_(std::max(available - line * 3.0f, minimumBuildings));
        Hint("Select a building to see its lots. Double-click a building that has one lot to plop it. "
             "While you plop, one click changes to it.");
        return;
    }
    const float lotRows = static_cast<float>(selectedBuilding_->lots.size()) + 1.5f;
    const float lotsHeight = std::clamp(lotRows * ImGui::GetFrameHeightWithSpacing(), line * 4.0f,
                                        std::max(line * 4.0f, available * 0.4f));
    RenderBuildingsTable_(std::max(available - lotsHeight - line * 2.0f, minimumBuildings));
    ImGui::Separator();
    RenderLotsDetailTable_(lotsHeight);
}

void BuildingsPanelTab::RenderResultLine_() {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Showing %zu of %zu buildings", filteredBuildings_.size(), lots_->GetBuildings().size());
    const int active = filter_.ActiveFilterCount();
    if (active > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("%d filter%s on", active, active == 1 ? "" : "s");
    }
    ImGui::SameLine(0.0f, kGroupGap);
    ImGui::BeginDisabled(active == 0);
    if (ImGui::SmallButton("Clear filters")) {
        filter_.ResetFilters();
    }
    ImGui::EndDisabled();
    Tooltip(active == 0 ? "No filters are on." : "Show all buildings again.");
}

void BuildingsPanelTab::OnDeviceReset(uint32_t deviceGeneration) {
    if (deviceGeneration != lastDeviceGeneration_) {
        thumbnailCache_.OnDeviceReset();
        previewCache_.OnDeviceReset();
        lastDeviceGeneration_ = deviceGeneration;
    }
}

void BuildingsPanelTab::HandlePickedLot_(const PickedLot& picked) {
    const Building* building = lots_->FindBuildingByLotInstanceId(picked.lotInstanceId);
    if (!building) {
        LOG_WARN("Picked lot 0x{:08X}, but it is not present in the lot cache", picked.lotInstanceId);
        return;
    }

    filter_ = LotFilterHelper{};
    filter_.searchBuffer = building->name;
    selectedBuilding_ = building;
}

ImGuiTexture BuildingsPanelTab::LoadBuildingTexture_(uint64_t buildingKey) {
    ImGuiTexture texture;

    if (!imguiService_) {
        LOG_WARN("Could not load building thumbnail: imguiService_ is null");
        return texture;
    }

    auto data = lots_->GetBuildingThumbnailStore().LoadThumbnail(buildingKey, static_cast<uint32_t>(UI::kIconSize));
    if (!data.has_value()) {
        LOG_WARN("Building thumbnail not found in store for key 0x{:016X}", buildingKey);
        return texture;
    }

    texture.Create(imguiService_, data->width, data->height, data->rgba.data());
    return texture;
}

void BuildingsPanelTab::RenderFilterUI_() {
    static char searchBuf[256] = {};
    if (filter_.searchBuffer != searchBuf) {
        std::strncpy(searchBuf, filter_.searchBuffer.c_str(), sizeof(searchBuf) - 1);
        searchBuf[sizeof(searchBuf) - 1] = '\0';
    }

    // Leave room on the right for the pick button.
    const float kPickButtonWidth = ImGui::CalcTextSize("Stop picking").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(-(kPickButtonWidth + ImGui::GetStyle().ItemSpacing.x));
    if (ImGui::InputTextWithHint("##SearchBuildings", "Search buildings...", searchBuf, sizeof(searchBuf))) {
        filter_.searchBuffer = searchBuf;
    }
    PanelUi::Tooltip(PanelUi::kSearchHelp);

    ImGui::SameLine();
    if (director_->IsLotPicking()) {
        if (ImGui::Button("Stop picking##LotPicker", ImVec2(kPickButtonWidth, 0.0f))) {
            director_->StopLotPicking();
        }
    }
    else {
        if (ImGui::Button("Pick lot", ImVec2(kPickButtonWidth, 0.0f))) {
            ReleaseImGuiInputCapture_();
            director_->StartLotPicking([this](const PickedLot& picked) {
                HandlePickedLot_(picked);
            });
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Click a plopped lot in the city to select it in the list.\nPress Esc to cancel.");
        }
    }

    // Zone type filter
    const char* zoneTypes[] = {
        "Any zone", "Residential (R)", "Commercial (C)", "Industrial (I)", "Plopped", "None", "Other"
    };
    int currentZone = filter_.selectedZoneType.has_value() ? (filter_.selectedZoneType.value() + 1) : 0;
    ImGui::SetNextItemWidth(UI::dropdownWidth());
    if (ImGui::Combo("##ZoneType", &currentZone, zoneTypes, 7)) {
        if (currentZone == 0) {
            filter_.selectedZoneType.reset();
        } else {
            filter_.selectedZoneType = static_cast<uint8_t>(currentZone - 1);
        }
    }


    // Wealth filter
    NextInRow(1, UI::dropdownWidth());
    const char* wealthOptions[] = {"Any wealth", "$", "$$", "$$$"};
    int currentWealth = filter_.selectedWealthType.has_value() ? filter_.selectedWealthType.value() : 0;
    ImGui::SetNextItemWidth(UI::dropdownWidth());
    if (ImGui::Combo("##Wealth", &currentWealth, wealthOptions, 4)) {
        if (currentWealth == 0) {
            filter_.selectedWealthType.reset();
        } else {
            filter_.selectedWealthType = static_cast<uint8_t>(currentWealth);
        }
    }


    // Growth stage filter
    NextInRow(2, UI::dropdownWidth());
    const char* growthStages[] = {
        "Any stage", "Plopped (255)", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15"
    };
    auto currentGrowthStageIndex = 0;
    if (filter_.selectedGrowthStage.has_value()) {
        uint8_t val = filter_.selectedGrowthStage.value();
        if (val == 255) {
            currentGrowthStageIndex = 1;
        } else if (val <= 15) {
            currentGrowthStageIndex = val + 2;
        }
    }
    ImGui::SetNextItemWidth(UI::dropdownWidth());
    if (ImGui::Combo("##GrowthStage", &currentGrowthStageIndex, growthStages, 18)) {
        if (currentGrowthStageIndex == 0) {
            filter_.selectedGrowthStage.reset();
        } else if (currentGrowthStageIndex == 1) {
            filter_.selectedGrowthStage = 255;
        } else {
            filter_.selectedGrowthStage = static_cast<uint8_t>(currentGrowthStageIndex - 2);
        }
    }
    Tooltip("The growth stage of the lot. Plopped lots have stage 255.");

    NextInRow(3, UI::dropdownWidth());
    const char* wallToWallOptions[] = {"Any W2W", "W2W only", "Non-W2W only"};
    int currentWallToWall = static_cast<int>(filter_.wallToWallFilter);
    ImGui::SetNextItemWidth(UI::dropdownWidth());
    if (ImGui::Combo("##WallToWall", &currentWallToWall, wallToWallOptions, 3)) {
        filter_.wallToWallFilter = static_cast<LotFilterHelper::WallToWallFilter>(currentWallToWall);
    }
    Tooltip("W2W means wall-to-wall: buildings made to stand against each other.");

    NextInRow(4, CheckboxWidth("Favorites only"));
    ImGui::Checkbox("Favorites only", &filter_.favoritesOnly);

    // Size filters, in tiles. Each label stays with its slider when the row wraps.
    const float sizeWidth = ImGui::CalcTextSize("Width (tiles)").x + ImGui::GetStyle().ItemSpacing.x + UI::wideInputWidth();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Width (tiles)");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(UI::wideInputWidth());
    ImGui::SliderInt2("##SizeX", filter_.sizeX, LotSize::kMinSize, LotSize::kMaxSize);
    NextInRow(1, sizeWidth, kGroupGap);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Depth (tiles)");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(UI::wideInputWidth());
    ImGui::SliderInt2("##SizeZ", filter_.sizeZ, LotSize::kMinSize, LotSize::kMaxSize);

    ImGui::Separator();
    RenderBuildingStyleFilter_();
    RenderOccupantGroupFilter_();
}

void BuildingsPanelTab::RenderBuildingsTable_(const float tableHeight) {
    director_->IsLotPlopping();  // watches for the game's tool to take over after a plop starts
    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_Sortable |
        ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTable("BuildingsTable", 4, tableFlags, ImVec2(0, tableHeight))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##Thumb",
                                ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                UI::iconColumnWidth());
        ImGui::TableSetupColumn("Name",
                                ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_DefaultSort |
                                ImGuiTableColumnFlags_PreferSortAscending);
        ImGui::TableSetupColumn("Description",
                                ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Lots",
                                ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                UI::lotsCountColumnWidth());
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty) {
            if (specs->SpecsCount > 0) {
                sortDescending_ = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
                SortBuildings_();
            }
            specs->SpecsDirty = false;
        }

        // Use clipper for virtualized scrolling
        const float rowHeight = UI::iconRowHeight();
        ImGuiListClipper clipper;
        // A row that its content fills also gets the cell padding. A set height means no row is
        // drawn each frame only to measure it, which would also pre-load the top thumbnails.
        clipper.Begin(static_cast<int>(filteredBuildings_.size()), rowHeight + ImGui::GetStyle().CellPadding.y * 2.0f);

        while (clipper.Step()) {
            // Request texture loads for visible + margin items
            const int prefetchStart = std::max(0, clipper.DisplayStart - Cache::kPrefetchMargin);
            const int prefetchEnd = std::min(static_cast<int>(filteredBuildings_.size()),
                                             clipper.DisplayEnd + Cache::kPrefetchMargin);

            for (int i = prefetchStart; i < prefetchEnd; ++i) {
                const auto& building = filteredBuildings_[i];
                const uint64_t key = MakeGIKey(building->groupId.value(), building->instanceId.value());
                if (lots_->GetBuildingThumbnailStore().HasThumbnail(key)) {
                    thumbnailCache_.Request(key);
                }
            }

            // Render visible rows
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto* building = filteredBuildings_[i];
                const bool isSelected = (building == selectedBuilding_);
                RenderBuildingRow_(*building, isSelected);
            }
        }

        // Process load queue each frame
        thumbnailCache_.ProcessLoadQueue([this](const uint64_t key) {
            return LoadBuildingTexture_(key);
        });

        ImGui::EndTable();
    }
}

void BuildingsPanelTab::RenderLotsDetailTable_(const float tableHeight) {
    ImGui::Text("Lots for: %s (%zu lots)", selectedBuilding_->name.c_str(), selectedBuilding_->lots.size());

    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTable("LotsDetailTable", 4, tableFlags, ImVec2(0, tableHeight))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoHide);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, UI::lotSizeColumnWidth());
        ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthFixed, UI::lotStageColumnWidth());
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                UI::actionColumnWidth());
        ImGui::TableHeadersRow();

        for (const auto& lot : selectedBuilding_->lots) {
            RenderLotRow_(lot);
        }

        ImGui::EndTable();
    }
}

void BuildingsPanelTab::RenderBuildingRow_(const Building& building, const bool isSelected) {
    const uint64_t key = MakeGIKey(building.groupId.value(), building.instanceId.value());

    ImGui::PushID(static_cast<int>(key));
    const float rowHeight = UI::iconRowHeight();
    ImGui::TableNextRow(0, rowHeight);

    // Thumbnail column — place a full-height Selectable first so the
    // highlight covers the entire row, then overlay the thumbnail.
    ImGui::TableNextColumn();
    if (ImGui::Selectable("##row", isSelected,
                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap |
                          ImGuiSelectableFlags_AllowDoubleClick,
                          ImVec2(0, rowHeight))) {
        selectedBuilding_ = &building;
        if ((ImGui::IsMouseDoubleClicked(0) || director_->IsLotPlopping()) && building.lots.size() == 1) {
            ReleaseImGuiInputCapture_();
            director_->TriggerLotPlop(building.lots[0].instanceId.value());
        }
    }
    ImGui::SameLine();
    auto thumbTextureId = thumbnailCache_.Get(key);
    RenderThumbnailWithPreview_(thumbTextureId, lots_->GetBuildingThumbnailStore(), key);

    // Name column
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(building.name.c_str());
    if (BuildingStyleResolver::IsWallToWall(building, GetBuildingStyleContext_())) {
        Badges::RenderPill("W2W", Badges::kWallToWallColor, Badges::kWallToWallHoverColor);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Wall-to-wall building");
        }
    }

    // Description column
    ImGui::TableNextColumn();
    if (!building.description.empty()) {
        // As many whole lines as fit, so that every row has the same height and the list does not
        // jump while it scrolls. The rest is in the tooltip.
        const auto res = CollapseConsecutiveNewlines(building.description);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const float lineHeight = ImGui::GetTextLineHeight();
        const float height = std::max(lineHeight, std::floor(rowHeight / lineHeight) * lineHeight -
                                                      ImGui::GetStyle().CellPadding.y * 2.0f);
        const ImVec4 clip(pos.x, pos.y, pos.x + width, pos.y + height);
        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), pos, ImGui::GetColorU32(ImGuiCol_Text),
                                            res.c_str(), nullptr, width, &clip);
        ImGui::Dummy(ImVec2(width, height));
        // The row's selectable spans this cell, so this tests the rectangle rather than the item.
        if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax())) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
            ImGui::TextUnformatted(res.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    // Lots count column
    ImGui::TableNextColumn();
    ImGui::Text("%zu", building.lots.size());

    ImGui::PopID();
}

void BuildingsPanelTab::RenderLotRow_(const Lot& lot) {
    ImGui::PushID(static_cast<int>(lot.instanceId.value()));
    ImGui::TableNextRow();

    // Name: a selectable for the full-row highlight. A double-click plops, or one click while plopping.
    ImGui::TableNextColumn();
    if (ImGui::Selectable(lot.name.c_str(), false,
                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap |
                          ImGuiSelectableFlags_AllowDoubleClick) &&
        (ImGui::IsMouseDoubleClicked(0) || director_->IsLotPlopping())) {
        ReleaseImGuiInputCapture_();
        director_->TriggerLotPlop(lot.instanceId.value());
    }

    // Size
    ImGui::TableNextColumn();
    ImGui::Text("%d x %d", lot.sizeX, lot.sizeZ);

    // Growth stage
    ImGui::TableNextColumn();
    if (lot.growthStage == 255) {
        ImGui::Text("Plop");
    } else {
        ImGui::Text("%d", lot.growthStage);
    }

    // Actions
    ImGui::TableNextColumn();
    if (ImGui::SmallButton("Plop")) {
        ReleaseImGuiInputCapture_();
        director_->TriggerLotPlop(lot.instanceId.value());
    }
    ImGui::SameLine();
    RenderFavButton_(lot.instanceId.value());

    ImGui::PopID();
}

void BuildingsPanelTab::RenderOccupantGroupFilter_() {
    // The count shows while the section is closed, so a filter on is never out of sight.
    const std::string header = filter_.selectedOccupantGroups.empty()
        ? std::string("Occupant Groups###OccupantGroups")
        : std::format("Occupant Groups ({} selected)###OccupantGroups", filter_.selectedOccupantGroups.size());

    std::function<void(const OccupantGroup&)> renderOGNode = [&](const OccupantGroup& og) {
        if (!og.children.empty()) {
            bool nodeOpen = ImGui::TreeNode(reinterpret_cast<void*>(static_cast<intptr_t>(og.id)),
                                            "%s", og.name.data());
            if (nodeOpen) {
                for (const auto& child : og.children) {
                    renderOGNode(child);
                }
                ImGui::TreePop();
            }
        } else {
            bool isSelected = filter_.selectedOccupantGroups.contains(og.id);
            if (ImGui::Checkbox(og.name.data(), &isSelected)) {
                if (isSelected) {
                    filter_.selectedOccupantGroups.insert(og.id);
                } else {
                    filter_.selectedOccupantGroups.erase(og.id);
                }
            }
        }
    };

    if (ImGui::CollapsingHeader(header.c_str())) {
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, UI::treeIndentSpacing());

        if (ImGui::BeginChild("##OGTree", ImVec2(0, UI::ogTreeHeight()), true)) {
            for (const auto& og : OCCUPANT_GROUP_TREE) {
                renderOGNode(og);
            }
        }
        ImGui::EndChild();

        ImGui::BeginDisabled(filter_.selectedOccupantGroups.empty());
        if (ImGui::SmallButton("Clear occupant groups")) {
            filter_.selectedOccupantGroups.clear();
        }
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
    }
}

void BuildingsPanelTab::RenderBuildingStyleFilter_() {
    const auto& styles = GetBuildingStyles_();
    const auto& context = GetBuildingStyleContext_();

    // The list of styles is only final in a city. Keep what was selected until then.
    if (context.availabilityKnown) {
        std::erase_if(filter_.selectedBuildingStyles, [&](const uint32_t styleId) {
            return !context.availableStyleIds.contains(styleId);
        });
    }

    // The count shows while the section is closed, so a filter on is never out of sight.
    const size_t selectedStyles = filter_.selectedBuildingStyles.size();
    const std::string header =
        selectedStyles == 0 && !filter_.activeStylesOnly ? std::string("Building Styles###BuildingStyles")
        : selectedStyles == 0 ? std::string("Building Styles (active styles only)###BuildingStyles")
                              : std::format("Building Styles ({} selected)###BuildingStyles", selectedStyles);

    if (ImGui::CollapsingHeader(header.c_str())) {
        const bool styleApiAvailable = buildingStyleService_->HasStyleInfoApi();
        const bool runtimeCatalogReady = styleApiAvailable && !buildingStyleService_->GetAvailableStyles().empty();

        if (cacheNeedsRebuild_) {
            Hint("Rebuild the plugin cache to get the style data of your buildings.");
        }

        if (!styleApiAvailable) {
            Hint("Only the Maxis styles show. The More Building Styles DLL is not installed.");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("To use custom styles, install SC4MoreBuildingStyles.dll.\n"
                                  "Also install the Building Style Control and the style definition files for your setup.");
            }
        }
        else {
            if (!runtimeCatalogReady) {
                Hint("Load a city to see the custom styles and the active styles.");
            }

            ImGui::BeginDisabled(!context.activeStylesKnown);
            ImGui::Checkbox("Compatible with active styles", &filter_.activeStylesOnly);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", context.activeStylesKnown
                                            ? "Show only buildings with a style that is checked in the Building Style Control."
                                            : "Load a city to use this filter.");
            }
        }

        if (ImGui::BeginChild("##BuildingStyleList", ImVec2(0, UI::ogTreeHeight()), true)) {
            for (const auto& style : styles) {
                bool selected = filter_.selectedBuildingStyles.contains(style.id);
                const std::string label = std::format("{}##BuildingStyle{:08X}", style.name, style.id);
                if (ImGui::Checkbox(label.c_str(), &selected)) {
                    if (selected) {
                        filter_.selectedBuildingStyles.insert(style.id);
                    }
                    else {
                        filter_.selectedBuildingStyles.erase(style.id);
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Style ID: 0x%08X", style.id);
                }
            }
        }
        ImGui::EndChild();

        ImGui::BeginDisabled(selectedStyles == 0 && !filter_.activeStylesOnly);
        if (ImGui::SmallButton("Clear styles")) {
            filter_.selectedBuildingStyles.clear();
            filter_.activeStylesOnly = false;
        }
        ImGui::EndDisabled();
    }
}

void BuildingsPanelTab::RenderFavButton_(const uint32_t lotInstanceId) const {
    const bool isFavorite = favorites_->IsLotFavorite(lotInstanceId);
    const char* label = isFavorite ? "Unstar" : "Star";

    if (ImGui::SmallButton(label)) {
        favorites_->ToggleLotFavorite(lotInstanceId);
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(isFavorite ? "Remove from favorites" : "Add to favorites");
    }
}

void BuildingsPanelTab::ApplyFilters_() {
    const auto& buildings = lots_->GetBuildings();
    const auto& favoriteLots = favorites_->GetFavoriteLotIds();
    const auto& context = GetBuildingStyleContext_();

    // Thousands of buildings: only when something changed, not every frame.
    const size_t favoriteCount = filter_.favoritesOnly ? favoriteLots.size() : 0;
    if (buildings.size() == appliedBuildingCount_ && favoriteCount == appliedFavoriteCount_ &&
        filter_ == appliedFilter_ && context == appliedContext_) {
        return;
    }
    if (buildings.size() != appliedBuildingCount_) {
        lotSearchTexts_.clear();
        suggester_.Clear();
        for (const auto& building : buildings) {
            for (const Lot& lot : building.lots) {
                std::string& text = lotSearchTexts_[&lot];
                text = TextSearch::Normalize(lot.name + ' ' + building.name + TextSearch::HexId(lot.instanceId.value()) +
                                             TextSearch::HexId(building.instanceId.value()));
                suggester_.Add(text);
            }
        }
    }
    appliedBuildingCount_ = buildings.size();
    appliedFavoriteCount_ = favoriteCount;
    appliedFilter_ = filter_;
    appliedContext_ = context;

    filteredBuildings_.clear();
    filteredBuildings_.reserve(buildings.size());

    const TextSearch::Query query(filter_.searchBuffer);
    for (const auto& building : buildings) {
        // A building passes if any of its lots passes all filters
        bool hasMatchingLot = std::ranges::any_of(building.lots, [&](const Lot& lot) {
            const auto text = lotSearchTexts_.find(&lot);
            LotView view{&building, &lot, text == lotSearchTexts_.end() ? nullptr : &text->second, &query};
            return filter_.PassesFilters(view, context) &&
                   (!filter_.favoritesOnly || favoriteLots.contains(lot.instanceId.value()));
        });

        if (hasMatchingLot) {
            filteredBuildings_.push_back(&building);
        }
    }

    SortBuildings_();

    // Clear selection if it was filtered out
    if (selectedBuilding_) {
        bool found = std::ranges::any_of(filteredBuildings_, [this](const Building* b) {
            return b == selectedBuilding_;
        });
        if (!found) {
            selectedBuilding_ = nullptr;
        }
    }
}

void BuildingsPanelTab::SortBuildings_() {
    if (sortDescending_) {
        std::ranges::sort(filteredBuildings_, [](const Building* a, const Building* b) { return a->name > b->name; });
    }
    else {
        std::ranges::sort(filteredBuildings_, [](const Building* a, const Building* b) { return a->name < b->name; });
    }
}

const std::vector<BuildingStyleEntry>& BuildingsPanelTab::GetBuildingStyles_() const {
    static const std::vector<BuildingStyleEntry> kMaxisStyles{
        {0x2000, "Chicago 1890"},
        {0x2003, "Euro-Contemporary"},
        {0x2002, "Houston 1990"},
        {0x2001, "New York 1940"},
    };
    const auto& available = buildingStyleService_->GetAvailableStyles();
    return available.empty() ? kMaxisStyles : available;
}

const BuildingStyleContext& BuildingsPanelTab::GetBuildingStyleContext_() const {
    return buildingStyleService_->GetContext();
}
