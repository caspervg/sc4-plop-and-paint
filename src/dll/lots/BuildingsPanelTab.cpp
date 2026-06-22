#include "BuildingsPanelTab.hpp"

#include <format>

#include "OccupantGroups.hpp"
#include "../common/BadgeUtils.hpp"
#include "../common/Utils.hpp"
#include "../utils/Logger.h"

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
    InitializeFallbackBuildingStyles_();
}

const char* BuildingsPanelTab::GetTabName() const {
    return "Buildings & Lots";
}

void BuildingsPanelTab::OnRender() {
    const auto& buildings = lots_->GetBuildings();

    if (buildings.empty()) {
        ImGui::TextUnformatted("No buildings loaded. Please ensure lots.cbor exists in the Plugins directory.");
        return;
    }

    RenderFilterUI_();
    ImGui::Separator();

    ApplyFilters_();
    ImGui::Text("Showing %zu of %zu buildings", filteredBuildings_.size(), buildings.size());

    // Calculate available height for tables
    const float availHeight = ImGui::GetContentRegionAvail().y;
    const float buildingTableHeight = availHeight * 0.6f;
    const float lotsTableHeight = availHeight * 0.4f - ImGui::GetTextLineHeightWithSpacing();

    RenderBuildingsTable_(buildingTableHeight);

    ImGui::Separator();

    RenderLotsDetailTable_(lotsTableHeight);
}

void BuildingsPanelTab::OnDeviceReset(uint32_t deviceGeneration) {
    if (deviceGeneration != lastDeviceGeneration_) {
        thumbnailCache_.OnDeviceReset();
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

    auto data = lots_->GetBuildingThumbnailStore().LoadThumbnail(buildingKey);
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
    constexpr float kPickButtonWidth = 100.0f;
    ImGui::SetNextItemWidth(-(kPickButtonWidth + ImGui::GetStyle().ItemSpacing.x));
    if (ImGui::InputTextWithHint("##SearchBuildings", "Search buildings...", searchBuf, sizeof(searchBuf))) {
        filter_.searchBuffer = searchBuf;
    }

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
            ImGui::SetTooltip("Click a plopped lot in the city to select it in the list.\nESC or RMB to cancel.");
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

    ImGui::SameLine();

    // Wealth filter
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

    ImGui::SameLine();

    // Growth stage filter
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

    ImGui::SameLine();

    const char* wallToWallOptions[] = {"Any W2W", "W2W only", "Non-W2W only"};
    int currentWallToWall = static_cast<int>(filter_.wallToWallFilter);
    ImGui::SetNextItemWidth(UI::dropdownWidth());
    if (ImGui::Combo("##WallToWall", &currentWallToWall, wallToWallOptions, 3)) {
        filter_.wallToWallFilter = static_cast<LotFilterHelper::WallToWallFilter>(currentWallToWall);
    }

    ImGui::SameLine();
    ImGui::Checkbox("Favorites only", &filter_.favoritesOnly);

    // Size filters
    ImGui::Text("Width:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(UI::wideInputWidth());
    ImGui::SliderInt2("##SizeX", filter_.sizeX, LotSize::kMinSize, LotSize::kMaxSize);
    ImGui::SameLine();
    ImGui::Text("Depth:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(UI::wideInputWidth());
    ImGui::SliderInt2("##SizeZ", filter_.sizeZ, LotSize::kMinSize, LotSize::kMaxSize);

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    if (ImGui::Button("Clear filters")) {
        filter_.ResetFilters();
        searchBuf[0] = '\0';
    }

    ImGui::Separator();
    RenderBuildingStyleFilter_();
    RenderOccupantGroupFilter_();
}

void BuildingsPanelTab::RenderBuildingsTable_(const float tableHeight) {
    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_Sortable |
        ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTable("BuildingsTable", 4, tableFlags, ImVec2(0, tableHeight))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Thumb",
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

        // Handle sorting
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty) {
            if (specs->SpecsCount > 0) {
                sortDescending_ = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
            }
            specs->SpecsDirty = false;
        }

        // Sort buildings
        if (sortDescending_) {
            std::ranges::sort(filteredBuildings_, [](const Building* a, const Building* b) {
                return a->name > b->name;
            });
        } else {
            std::ranges::sort(filteredBuildings_, [](const Building* a, const Building* b) {
                return a->name < b->name;
            });
        }

        // Use clipper for virtualized scrolling
        const float rowHeight = UI::iconRowHeight();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(filteredBuildings_.size()), rowHeight);

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
    if (!selectedBuilding_) {
        ImGui::TextDisabled("Select a building above to see its lots");
        return;
    }

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
        if (ImGui::IsMouseDoubleClicked(0) && building.lots.size() == 1) {
            ReleaseImGuiInputCapture_();
            director_->TriggerLotPlop(building.lots[0].instanceId.value());
        }
    }
    ImGui::SameLine();
    auto thumbTextureId = thumbnailCache_.Get(key);
    RenderThumbnail_(thumbTextureId);

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
        const auto res = CollapseConsecutiveNewlines(building.description);
        ImGui::TextWrapped("%s", res.c_str());
    }

    // Lots count column
    ImGui::TableNextColumn();
    ImGui::Text("%zu", building.lots.size());

    ImGui::PopID();
}

void BuildingsPanelTab::RenderLotRow_(const Lot& lot) {
    ImGui::PushID(static_cast<int>(lot.instanceId.value()));
    ImGui::TableNextRow();

    // Name — Selectable for full-row highlight and double-click to plop
    ImGui::TableNextColumn();
    if (ImGui::Selectable(lot.name.c_str(), false,
                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap |
                          ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0)) {
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
    std::string preview;
    if (filter_.selectedOccupantGroups.empty()) {
        preview = "All Occupant Groups";
    } else {
        preview = std::to_string(filter_.selectedOccupantGroups.size()) + " selected";
    }

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

    if (ImGui::CollapsingHeader("Occupant Groups")) {
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, UI::treeIndentSpacing());
        ImGui::Text("%s", preview.c_str());

        if (ImGui::BeginChild("##OGTree", ImVec2(0, UI::ogTreeHeight()), true)) {
            for (const auto& og : OCCUPANT_GROUP_TREE) {
                renderOGNode(og);
            }
        }
        ImGui::EndChild();

        if (ImGui::SmallButton("Clear OGs")) {
            filter_.selectedOccupantGroups.clear();
        }
        ImGui::PopStyleVar();
    }
}

void BuildingsPanelTab::RenderBuildingStyleFilter_() {
    const auto& styles = GetBuildingStyles_();
    const auto& context = GetBuildingStyleContext_();

    std::unordered_set<uint32_t> visibleStyleIds;
    visibleStyleIds.reserve(styles.size());
    for (const auto& style : styles) {
        visibleStyleIds.insert(style.id);
    }
    std::erase_if(filter_.selectedBuildingStyles, [&](const uint32_t styleId) {
        return !visibleStyleIds.contains(styleId);
    });

    std::string preview = filter_.selectedBuildingStyles.empty()
        ? "All building styles"
        : std::to_string(filter_.selectedBuildingStyles.size()) + " selected";

    if (ImGui::CollapsingHeader("Building Styles")) {
        ImGui::TextUnformatted(preview.c_str());

        const bool styleApiAvailable = buildingStyleService_ && buildingStyleService_->HasStyleInfoApi();
        const bool runtimeCatalogReady = styleApiAvailable && !buildingStyleService_->GetAvailableStyles().empty();

        if (!styleApiAvailable) {
            ImGui::TextDisabled("Custom style names unavailable; showing cached style IDs.");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Install and enable SC4MoreBuildingStyles.dll plus the Building Style Control and style "
                    "definition files used by your setup to display custom style names.");
            }
        }
        else {
            if (!runtimeCatalogReady) {
                ImGui::TextDisabled("Load a city to resolve runtime style names and active styles.");
            }

            ImGui::BeginDisabled(!context.activeStylesKnown);
            ImGui::Checkbox("Compatible with active styles", &filter_.activeStylesOnly);
            ImGui::EndDisabled();
            if (!context.activeStylesKnown && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Active styles are available after a city has loaded.");
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

        if (ImGui::SmallButton("Clear styles")) {
            filter_.selectedBuildingStyles.clear();
            filter_.activeStylesOnly = false;
        }
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

    filteredBuildings_.clear();
    filteredBuildings_.reserve(buildings.size());

    for (const auto& building : buildings) {
        // A building passes if any of its lots passes all filters
        bool hasMatchingLot = std::ranges::any_of(building.lots, [&](const Lot& lot) {
            LotView view{&building, &lot};
            return filter_.PassesFilters(view, GetBuildingStyleContext_()) &&
                   (!filter_.favoritesOnly || favoriteLots.contains(lot.instanceId.value()));
        });

        if (hasMatchingLot) {
            filteredBuildings_.push_back(&building);
        }
    }

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

void BuildingsPanelTab::InitializeFallbackBuildingStyles_() {
    std::unordered_set<uint32_t> ids{0x2000, 0x2001, 0x2002, 0x2003};
    for (const auto& building : lots_->GetBuildings()) {
        if (building.buildingStyles.has_value()) {
            for (const uint32_t styleId : *building.buildingStyles) {
                if (!BuildingStyleResolver::IsReservedStyleId(styleId)) {
                    ids.insert(styleId);
                }
            }
        }
    }

    fallbackBuildingStyles_.reserve(ids.size());
    for (const uint32_t id : ids) {
        std::string name;
        switch (id) {
        case 0x2000: name = "Chicago 1890"; break;
        case 0x2001: name = "New York 1940"; break;
        case 0x2002: name = "Houston 1990"; break;
        case 0x2003: name = "Euro-Contemporary"; break;
        default: name = std::format("Style 0x{:X}", id); break;
        }
        fallbackBuildingStyles_.push_back({id, std::move(name)});
        fallbackBuildingStyleContext_.availableStyleIds.insert(id);
    }

    std::ranges::sort(fallbackBuildingStyles_, [](const BuildingStyleEntry& lhs, const BuildingStyleEntry& rhs) {
        return lhs.name < rhs.name;
    });
}

const std::vector<BuildingStyleEntry>& BuildingsPanelTab::GetBuildingStyles_() const {
    if (buildingStyleService_ && !buildingStyleService_->GetAvailableStyles().empty()) {
        return buildingStyleService_->GetAvailableStyles();
    }
    return fallbackBuildingStyles_;
}

const BuildingStyleContext& BuildingsPanelTab::GetBuildingStyleContext_() const {
    if (buildingStyleService_) {
        return buildingStyleService_->GetContext();
    }
    return fallbackBuildingStyleContext_;
}
