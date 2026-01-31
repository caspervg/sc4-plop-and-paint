#include "LotPlopPanel.hpp"

#include <functional>
#include <unordered_map>
#include "imgui_impl_win32.h"
#include "OccupantGroups.hpp"
#include "rfl/visit.hpp"
#include "spdlog/spdlog.h"


LotPlopPanel::LotPlopPanel(SC4AdvancedLotPlopDirector* director, cIGZImGuiService* imguiService)
    : director_(director), imguiService_(imguiService) {}

void LotPlopPanel::OnRender() {
    if (!isOpen_) {
        return;
    }

    ImGui::Begin("Advanced Lot Plop", &isOpen_);
    if (!isOpen_) {
        ImGui::End();
        return;
    }

    const auto& buildings = director_->GetBuildings();

    if (buildings.empty()) {
        ImGui::TextUnformatted("No lots loaded. Please ensure lot_configs.cbor exists in the Plugins directory.");
        ImGui::End();
        return;
    }

    // Check for device generation change (textures invalidated)
    if (imguiService_) {
        const uint32_t currentGen = imguiService_->GetDeviceGeneration();
        if (currentGen != lastDeviceGeneration_) {
            iconCache_.clear();
            texturesLoaded_ = false;
            lastDeviceGeneration_ = currentGen;
        }
    }

    // TabBar for future extensibility (Props, Flora, etc.)
    if (ImGui::BeginTabBar("AdvancedPlopTabs")) {
        if (ImGui::BeginTabItem("Lots")) {
            RenderLotsTab_();
            ImGui::EndTabItem();
        }
        // Future tabs: Props, Flora, etc.
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void LotPlopPanel::SetOpen(bool open) {
    isOpen_ = open;
}

bool LotPlopPanel::IsOpen() const {
    return isOpen_;
}

void LotPlopPanel::LoadIconTexture_(uint32_t buildingInstanceId, const Building& building) {
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

void LotPlopPanel::RenderFilterUI_() {
    // Search bar - use local buffer for ImGui compatibility
    static char searchBuf[256] = {};
    if (filterHelper_.searchBuffer != searchBuf) {
        std::strncpy(searchBuf, filterHelper_.searchBuffer.c_str(), sizeof(searchBuf) - 1);
        searchBuf[sizeof(searchBuf) - 1] = '\0';
    }

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
    ImGui::SetNextItemWidth(UI::kSliderWidth);
    ImGui::SliderInt("##MinSizeX", &filterHelper_.minSizeX, LotSize::kMinSize, LotSize::kMaxSize);
    ImGui::SameLine();
    ImGui::Text("to");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(UI::kSliderWidth);
    ImGui::SliderInt("##MaxSizeX", &filterHelper_.maxSizeX, LotSize::kMinSize, LotSize::kMaxSize);

    // Row 5: Depth filters
    ImGui::Text("Depth:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(UI::kSliderWidth);
    ImGui::SliderInt("##MinSizeZ", &filterHelper_.minSizeZ, LotSize::kMinSize, LotSize::kMaxSize);
    ImGui::SameLine();
    ImGui::Text("to");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(UI::kSliderWidth);
    ImGui::SliderInt("##MaxSizeZ", &filterHelper_.maxSizeZ, LotSize::kMinSize, LotSize::kMaxSize);

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

void LotPlopPanel::RenderTable_() {
    const auto& buildings = director_->GetBuildings();

    // Flatten for filtering
    std::vector<LotView> lotViews;
    size_t totalLots = 0;
    for (const auto& b : buildings) totalLots += b.lots.size();
    lotViews.reserve(totalLots);
    for (const auto& b : buildings) {
        for (const auto& lot : b.lots) {
            lotViews.push_back(LotView{&b, &lot});
        }
    }

    RenderTable_(lotViews, director_->GetFavoriteLotIds());
}

void LotPlopPanel::RenderTable_(const std::vector<LotView>& lotViews,
                                const std::unordered_set<uint32_t>& favorites) {
    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti;
    constexpr ImGuiTreeNodeFlags nodeFlagsBase = ImGuiTreeNodeFlags_DrawLinesFull;

    if (ImGui::BeginTable("LotsTable", 4, tableFlags, ImVec2(0, UI::kTableHeight))) {
        ImGui::TableSetupColumn("Name",
                                ImGuiTableColumnFlags_NoHide |
                                ImGuiTableColumnFlags_DefaultSort |
                                ImGuiTableColumnFlags_PreferSortAscending);
        ImGui::TableSetupColumn("Size",
                                ImGuiTableColumnFlags_WidthFixed |
                                ImGuiTableColumnFlags_PreferSortAscending,
                                UI::kSizeColumnWidth);
        ImGui::TableSetupColumn("Action",
                                ImGuiTableColumnFlags_WidthFixed |
                                ImGuiTableColumnFlags_NoSort,
                                UI::kActionColumnWidth);
        ImGui::TableSetupColumn("Thumbnail",
                                ImGuiTableColumnFlags_WidthFixed |
                                ImGuiTableColumnFlags_NoSort,
                                UI::kIconColumnWidth);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsCount > 0) {
            std::vector<LotFilterHelper::SortSpec> newSpecs;
            newSpecs.reserve(specs->SpecsCount);
            for (int i = 0; i < specs->SpecsCount; ++i) {
                const auto& s = specs->Specs[i];
                switch (s.ColumnIndex) {
                    case 0:
                        newSpecs.push_back({LotFilterHelper::SortColumn::Name,
                                            s.SortDirection == ImGuiSortDirection_Descending});
                        break;
                    case 1:
                        newSpecs.push_back({LotFilterHelper::SortColumn::Size,
                                            s.SortDirection == ImGuiSortDirection_Descending});
                        break;
                    default:
                        break;
                }
            }
            if (!newSpecs.empty()) {
                sortSpecs_ = std::move(newSpecs);
            }
            specs->SpecsDirty = false;
        }

        const auto filteredLots = filterHelper_.ApplyFiltersAndSort(lotViews, favorites, sortSpecs_);

        // Keep building iteration in sorted order.
        std::unordered_map<const Building*, std::vector<const LotView*>> grouped;
        grouped.reserve(filteredLots.size());
        std::vector<const Building*> buildingOrder;
        buildingOrder.reserve(filteredLots.size());
        for (const auto& v : filteredLots) {
            if (!grouped.contains(v.building)) {
                buildingOrder.push_back(v.building);
            }
            grouped[v.building].push_back(&v);
        }

        // Lazy-load icons for visible lots (prioritize sorted results)
        if (!texturesLoaded_ && imguiService_) {
            size_t loaded = 0;
            for (const auto& view : filteredLots) {
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

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const bool isMulti = lots.size() > 1;
            ImGuiTreeNodeFlags nodeFlags = nodeFlagsBase;

            if (isMulti) {
                nodeFlags |= ImGuiTreeNodeFlags_LabelSpanAllColumns;
                ImGui::PushID(building->instanceId.value());
                const bool open = ImGui::TreeNodeEx(building->name.c_str(), nodeFlags);
                if (!building->description.empty()) {
                    ImGui::SetItemTooltip("%s", building->description.c_str());
                }
                ImGui::PopID();
                auto const iconCacheKey = building->instanceId.value();

                if (open) {
                    ImGui::TableSetColumnIndex(3);
                    if (iconCache_.contains(iconCacheKey)) {
                        ImGui::Image(iconCache_[iconCacheKey].GetID(), ImVec2(UI::kIconSize, UI::kIconSize));
                    }
                    ImGui::TableSetColumnIndex(0);
                    for (const auto* v : lots) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::PushID(v->lot->instanceId.value());
                        ImGui::TreeNodeEx(v->lot->name.c_str(),
                                          ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet |
                                          ImGuiTreeNodeFlags_NoTreePushOnOpen);
                        ImGui::PopID();
                        ImGui::TableNextColumn();
                        ImGui::Text("%dx%d", v->lot->sizeX, v->lot->sizeZ);
                        ImGui::TableNextColumn();
                        if (ImGui::Button(("Plop##" + std::to_string(v->lot->instanceId.value())).c_str()))
                            director_->TriggerLotPlop(v->lot->instanceId.value());
                        ImGui::SameLine();
                        RenderFavButton_(v->lot->instanceId.value());
                    }
                    ImGui::TreePop();
                }
                else {
                    ImGui::TableSetColumnIndex(3);
                    if (iconCache_.contains(iconCacheKey)) {
                        ImGui::Image(iconCache_[iconCacheKey].GetID(), ImVec2(UI::kIconSize, UI::kIconSize));
                    }
                    else {
                        ImGui::Dummy(ImVec2(UI::kIconSize, UI::kIconSize));
                    }
                    ImGui::TableSetColumnIndex(0);
                }
            }
            else {
                const auto v = lots.front();
                nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                ImGui::Bullet();
                ImGui::BeginGroup();
                ImGui::Text(v->building->name.c_str());
                ImGui::TextDisabled(v->lot->name.c_str());
                ImGui::EndGroup();

                if (!building->description.empty()) {
                    ImGui::SetItemTooltip("%s", building->description.c_str());
                }
                ImGui::TableNextColumn();
                ImGui::Text("%dx%d", v->lot->sizeX, v->lot->sizeZ);
                ImGui::TableNextColumn();
                if (ImGui::Button(("Plop##" + std::to_string(v->lot->instanceId.value())).c_str()))
                    director_->TriggerLotPlop(v->lot->instanceId.value());
                ImGui::SameLine();
                RenderFavButton_(v->lot->instanceId.value());
                ImGui::TableNextColumn();
                auto const iconCacheKey = building->instanceId.value();
                if (iconCache_.contains(iconCacheKey)) {
                    ImGui::Image(iconCache_[iconCacheKey].GetID(), ImVec2(UI::kIconSize, UI::kIconSize));
                }
                else {
                    ImGui::Dummy(ImVec2(UI::kIconSize, UI::kIconSize));
                }
            }
        }
        ImGui::EndTable();
    }
}

void LotPlopPanel::RenderLotsTab_() {
    const auto& buildings = director_->GetBuildings();

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
    RenderTable_(lotViews, director_->GetFavoriteLotIds());
}

void LotPlopPanel::RenderFavButton_(const uint32_t lotInstanceId) const {
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

void LotPlopPanel::RenderOccupantGroupFilter_() {
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
