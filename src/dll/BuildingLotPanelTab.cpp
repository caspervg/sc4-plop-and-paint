#include "BuildingLotPanelTab.hpp"

#include <algorithm>
#include "Constants.hpp"
#include "OccupantGroups.hpp"
#include "rfl/visit.hpp"
#include "spdlog/spdlog.h"


const char* BuildingLotPanelTab::GetTabName() const {
    return "Buildings & Lots";
}

void BuildingLotPanelTab::OnRender() {
    const auto& buildings = director_->GetBuildings();

    if (buildings.empty()) {
        ImGui::TextUnformatted("No buildings/lots loaded. Please ensure lot_configs.cbor exists in the Plugins directory.");
        return;
    }

    if (iconCache_.empty() && !buildings.empty()) {
        iconCache_.reserve(buildings.size());
    }

    // Render filter UI
    RenderFilterUI_();

    ImGui::Separator();

    // Get filtered and sorted lots
    // Flatten buildings into lot views
    std::vector<LotView> lotViews;
    size_t totalLots = 0;
    for (const auto& b : buildings) {
        totalLots += b.lots.size();
    }
    lotViews.reserve(totalLots);
    for (const auto& b : buildings) {
        for (const auto& lot : b.lots) {
            lotViews.push_back(LotView{&b, &lot});
        }
    }

    const auto filteredLots = filterHelper_.ApplyFiltersAndSort(
        lotViews, director_->GetFavoriteLotIds(), sortSpecs_);

    // Show count
    ImGui::Text("Showing %zu of %zu lots, %zu buildings", filteredLots.size(), lotViews.size(),
                director_->GetBuildings().size());

    // Render table with pre-filtered data
    RenderTableInternal_(filteredLots, director_->GetFavoriteLotIds());
}

void BuildingLotPanelTab::OnDeviceReset(uint32_t deviceGeneration) {
    if (deviceGeneration != lastDeviceGeneration_) {
        iconCache_.clear();
        texturesLoaded_ = false;
        lastDeviceGeneration_ = deviceGeneration;
    }
}

void BuildingLotPanelTab::LoadIconTexture_(uint32_t buildingInstanceId, const Building& building) {
    if (!imguiService_ || !building.thumbnail.has_value()) {
        return;
    }

    // Already loaded?
    if (iconCache_.contains(buildingInstanceId)) {
        return;
    }

    // Extract RGBA32 data from the thumbnail variant
    const auto& thumbnail = building.thumbnail.value();

    // Use rfl::visit to handle the TaggedUnion
    rfl::visit([&](const auto& variant) {
        const auto& data = variant.data;
        const uint32_t width = variant.width;
        const uint32_t height = variant.height;

        if (data.empty() || width == 0 || height == 0) {
            return;
        }

        // Verify data size matches expected RGBA32 size
        const size_t expectedSize = static_cast<size_t>(width) * height * 4;
        if (data.size() != expectedSize) {
            spdlog::warn("Icon data size mismatch for building 0x{:08X}: expected {}, got {}",
                         buildingInstanceId, expectedSize, data.size());
            return;
        }

        // Create ImGui texture directly from pre-decoded RGBA32 data
        ImGuiTexture texture;
        if (texture.Create(imguiService_, width, height, data.data())) {
            iconCache_.emplace(buildingInstanceId, std::move(texture));
            spdlog::debug("Loaded icon for building 0x{:08X} ({}x{})",
                          buildingInstanceId, width, height);
        }
        else {
            spdlog::warn("Failed to create texture for building 0x{:08X}", buildingInstanceId);
        }
    }, thumbnail);
}

void BuildingLotPanelTab::RenderFilterUI_() {
    // Search bar - use local buffer for ImGui compatibility
    static char searchBuf[256] = {};

    // Row 1: Search bar (full width)
    ImGui::SetNextItemWidth(-1); // Full width
    if (ImGui::InputTextWithHint("##Search", "Search lots and buildings...", searchBuf, sizeof(searchBuf))) {
        filterHelper_.searchBuffer = searchBuf;
    }

    // Row 2: Zone type and Wealth
    const char* zoneTypes[] = {
        "Any zone", "Residential (R)", "Commercial (C)", "Industrial (I)", "Plopped", "None", "Other"
    };
    int currentZone = filterHelper_.selectedZoneType.has_value() ? (filterHelper_.selectedZoneType.value() + 1) : 0;
    ImGui::SetNextItemWidth(UI::kDropdownWidth);
    if (ImGui::Combo("##ZoneType", &currentZone, zoneTypes, 7)) {
        if (currentZone == 0) {
            filterHelper_.selectedZoneType.reset(); // Any zone
        }
        else {
            filterHelper_.selectedZoneType = static_cast<uint8_t>(currentZone - 1);
        }
    }

    ImGui::SameLine();

    const char* wealthOptions[] = {"Any wealth", "$", "$$", "$$$"};
    int currentWealth = filterHelper_.selectedWealthType.has_value() ? filterHelper_.selectedWealthType.value() : 0;
    ImGui::SetNextItemWidth(UI::kDropdownWidth);
    if (ImGui::Combo("##Wealth", &currentWealth, wealthOptions, 4)) {
        if (currentWealth == 0) {
            filterHelper_.selectedWealthType.reset(); // Any
        }
        else {
            filterHelper_.selectedWealthType = static_cast<uint8_t>(currentWealth);
        }
    }

    ImGui::SameLine();

    // Growth stage filter dropdown (0-15 or 255 for ploppables)
    const char* growthStages[] = {
        "Any stage", "Plopped (255)", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14",
        "15"
    };
    auto currentGrowthStageIndex = 0;
    if (filterHelper_.selectedGrowthStage.has_value()) {
        uint8_t val = filterHelper_.selectedGrowthStage.value();
        if (val == 255) {
            currentGrowthStageIndex = 1; // Plopped
        }
        else if (val <= 15) {
            currentGrowthStageIndex = val + 2; // 0-15 mapped to indices 2-17
        }
    }
    ImGui::SetNextItemWidth(UI::kDropdownWidth);
    if (ImGui::Combo("##GrowthStage", &currentGrowthStageIndex, growthStages, 18)) {
        if (currentGrowthStageIndex == 0) {
            filterHelper_.selectedGrowthStage.reset(); // Any
        }
        else if (currentGrowthStageIndex == 1) {
            filterHelper_.selectedGrowthStage = 255; // Plopped
        }
        else {
            filterHelper_.selectedGrowthStage = static_cast<uint8_t>(currentGrowthStageIndex - 2); // 0-15
        }
    }

    ImGui::SameLine();

    ImGui::Checkbox("Favorites only", &filterHelper_.favoritesOnly);

    // Row 4: Size filters
    ImGui::Text("Width:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(50.0f);
    if (ImGui::InputInt("##MinSizeX", &filterHelper_.minSizeX, 1, 1)) {
        filterHelper_.minSizeX = std::clamp(filterHelper_.minSizeX, LotSize::kMinSize, LotSize::kMaxSize);
    }
    ImGui::SameLine();
    ImGui::Text("to");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(50.0f);
    if (ImGui::InputInt("##MaxSizeX", &filterHelper_.maxSizeX, 1, 1)) {
        filterHelper_.maxSizeX = std::clamp(filterHelper_.maxSizeX, LotSize::kMinSize, LotSize::kMaxSize);
    }

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    // Row 5: Depth filters
    ImGui::Text("Depth:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(50.0f);
    if (ImGui::InputInt("##MinSizeZ", &filterHelper_.minSizeZ, 1, 1)) {
        filterHelper_.minSizeZ = std::clamp(filterHelper_.minSizeZ, LotSize::kMinSize, LotSize::kMaxSize);
    }
    ImGui::SameLine();
    ImGui::Text("to");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(50.0f);
    if (ImGui::InputInt("##MaxSizeZ", &filterHelper_.maxSizeZ, 1, 1)) {
        filterHelper_.maxSizeZ = std::clamp(filterHelper_.maxSizeZ, LotSize::kMinSize, LotSize::kMaxSize);
    }

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    // Clear Filters button
    if (ImGui::Button("Clear filters")) {
        filterHelper_.ResetFilters();
    }

    ImGui::Separator();

    // Occupant Groups tree view
    RenderOccupantGroupFilter_();
}

void BuildingLotPanelTab::RenderTable_() {
    const auto& buildings = director_->GetBuildings();
    std::vector<LotView> lotViews;
    size_t totalLots = 0;
    for (const auto& b : buildings) totalLots += b.lots.size();
    lotViews.reserve(totalLots);
    for (const auto& b : buildings) {
        for (const auto& lot : b.lots) {
            lotViews.push_back(LotView{&b, &lot});
        }
    }

    const auto filteredLots = filterHelper_.ApplyFiltersAndSort(
        lotViews, director_->GetFavoriteLotIds(), sortSpecs_);

    RenderTableInternal_(filteredLots, director_->GetFavoriteLotIds());
}

void BuildingLotPanelTab::RenderTableInternal_(const std::vector<LotView>& filteredLotViews,
                                               const std::unordered_set<uint32_t>& favorites) {
    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("LotsTable", 3, tableFlags, ImVec2(0, UI::kTableHeight))) {
        ImGui::TableSetupColumn("Name",
                                ImGuiTableColumnFlags_WidthFixed |
                                ImGuiTableColumnFlags_NoHide |
                                ImGuiTableColumnFlags_DefaultSort |
                                ImGuiTableColumnFlags_PreferSortAscending,
                                UI::kNameColumnWidth);
        ImGui::TableSetupColumn("Size",
                                ImGuiTableColumnFlags_WidthFixed |
                                ImGuiTableColumnFlags_PreferSortAscending,
                                UI::kSizeColumnWidth);
        ImGui::TableSetupColumn("Action",
                                ImGuiTableColumnFlags_WidthFixed |
                                ImGuiTableColumnFlags_NoSort,
                                UI::kActionColumnWidth);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsCount > 0) {
            std::vector<LotFilterHelper::SortSpec> newSpecs;
            newSpecs.reserve(specs->SpecsCount);
            for (auto i = 0; i < specs->SpecsCount; ++i) {
                const auto& s = specs->Specs[i];
                switch (s.ColumnIndex) {
                case 0:
                    newSpecs.push_back({
                        LotFilterHelper::SortColumn::Name,
                        s.SortDirection == ImGuiSortDirection_Descending
                    });
                    break;
                case 1:
                    newSpecs.push_back({
                        LotFilterHelper::SortColumn::Size,
                        s.SortDirection == ImGuiSortDirection_Descending
                    });
                    break;
                default:
                    break;
                }
            }
            if (!newSpecs.empty()) {
                sortSpecs_ = std::move(newSpecs);
                specs->SpecsDirty = false;
            }
        }

        // Keep building iteration in sorted order.
        std::unordered_map<const Building*, std::vector<const LotView*>> grouped;
        grouped.reserve(filteredLotViews.size());
        std::vector<const Building*> buildingOrder;
        buildingOrder.reserve(filteredLotViews.size());
        for (const auto& v : filteredLotViews) {
            if (!grouped.contains(v.building)) {
                buildingOrder.push_back(v.building);
            }
            grouped[v.building].push_back(&v);
        }

        // Lazy-load icons for visible lots (prioritize sorted results)
        if (!texturesLoaded_ && imguiService_) {
            size_t loaded = 0;
            for (const auto& view : filteredLotViews) {
                if (loaded >= kMaxIconsToLoadPerFrame) break;
                const auto& building = *view.building;
                if (building.thumbnail.has_value() &&
                    !iconCache_.contains(building.instanceId.value())) {
                    LoadIconTexture_(building.instanceId.value(), building);
                    loaded++;
                }
            }
            if (loaded == 0) {
                texturesLoaded_ = true;
            }
        }

        for (const auto* building : buildingOrder) {
            const auto& lots = grouped[building];
            const auto iconCacheKey = building->instanceId.value();

            ImGui::TableNextRow();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(40, 40, 40, 80));
            ImGui::TableNextColumn();
            ImGui::BeginGroup();
            if (iconCache_.contains(iconCacheKey)) {
                ImGui::Image(iconCache_[iconCacheKey].GetID(), ImVec2(UI::kIconSize, UI::kIconSize));
            }
            else {
                ImGui::Dummy(ImVec2(UI::kIconSize, UI::kIconSize));
            }
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::Text("-> %s", building->name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%zu lots", lots.size());
            if (!building->description.empty()) {
                std::string desc = building->description;
                std::ranges::replace(desc, '\n', ' ');
                std::ranges::replace(desc, '\r', ' ');
                if (!desc.empty()) {
                    ImGui::TextDisabled("   %s", desc.c_str());
                }
            }
            ImGui::EndGroup();
            ImGui::EndGroup();
            ImGui::TableNextColumn(); // Size column (empty for building header)
            ImGui::TableNextColumn(); // Action column (empty for building header)

            for (size_t i = 0; i < lots.size(); ++i) {
                auto symbol = "|-";
                if (i == lots.size() - 1) {
                    symbol = "`-";
                }
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Dummy(ImVec2(UI::kIconSize, 1));
                ImGui::SameLine();
                ImGui::Text("%s %s", symbol, lots[i]->lot->name.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%d x %d", lots[i]->lot->sizeX, lots[i]->lot->sizeZ);
                ImGui::TableNextColumn();
                if (ImGui::Button(("Plop##" + std::to_string(lots[i]->lot->instanceId.value())).c_str())) {
                    director_->TriggerLotPlop(lots[i]->lot->instanceId.value());
                }
                ImGui::SameLine();
                RenderFavButton_(lots[i]->lot->instanceId.value());
            }
        }
        ImGui::EndTable();
    }
}

void BuildingLotPanelTab::RenderFavButton_(const uint32_t lotInstanceId) const {
    const bool isFavorite = director_->IsFavorite(lotInstanceId);
    const char* label = isFavorite ? "Unstar" : "Star";

    const std::string buttonLabel = std::string(label) + "##fav" + std::to_string(lotInstanceId);
    if (ImGui::Button(buttonLabel.c_str())) {
        director_->ToggleFavorite(lotInstanceId);
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(isFavorite ? "Remove from favorites" : "Add to favorites");
    }
}

void BuildingLotPanelTab::RenderOccupantGroupFilter_() {
    // Build preview string
    std::string preview;
    if (filterHelper_.selectedOccupantGroups.empty()) {
        preview = "All Occupant Groups";
    }
    else {
        preview = std::to_string(filterHelper_.selectedOccupantGroups.size()) + " selected";
    }

    // Recursive function to render tree nodes
    std::function<void(const OccupantGroup&)> renderOGNode = [&](const OccupantGroup& og) {
        // Check if this OG has children
        if (!og.children.empty()) {
            // Parent node with children - use TreeNode
            bool nodeOpen = ImGui::TreeNode(reinterpret_cast<void*>(static_cast<intptr_t>(og.id)),
                                            "%s", og.name.data());

            if (nodeOpen) {
                // Render children recursively
                for (const auto& child : og.children) {
                    renderOGNode(child);
                }
                ImGui::TreePop();
            }
        }
        else {
            // Leaf node - show checkbox
            bool isSelected = filterHelper_.selectedOccupantGroups.contains(og.id);
            if (ImGui::Checkbox(og.name.data(), &isSelected)) {
                if (isSelected) {
                    filterHelper_.selectedOccupantGroups.insert(og.id);
                }
                else {
                    filterHelper_.selectedOccupantGroups.erase(og.id);
                }
            }
        }
    };

    // Render as collapsible section (collapsed by default)
    if (ImGui::CollapsingHeader("Occupant Groups")) {
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 12.0f); // More compact indentation
        ImGui::Text("%s", preview.c_str());

        // Add scrollable region for the tree
        if (ImGui::BeginChild("##OGTree", ImVec2(0, 150), true)) {
            for (const auto& og : OCCUPANT_GROUP_TREE) {
                renderOGNode(og);
            }
        }
        ImGui::EndChild();

        // Clear all button
        if (ImGui::SmallButton("Clear OGs")) {
            filterHelper_.selectedOccupantGroups.clear();
        }
        ImGui::PopStyleVar();
    }
}

