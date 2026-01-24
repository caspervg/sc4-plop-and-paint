#include "LotPlopPanel.hpp"

#include <functional>
#include "imgui_impl_win32.h"
#include "OccupantGroups.hpp"
#include "rfl/visit.hpp"
#include "spdlog/spdlog.h"

LotPlopPanel::LotPlopPanel(SC4AdvancedLotPlopDirector* director, cIGZImGuiService* imguiService)
    : director_(director), imguiService_(imguiService) {}

void LotPlopPanel::SetOpen(bool open) {
    isOpen_ = open;
}

bool LotPlopPanel::IsOpen() const {
    return isOpen_;
}

void LotPlopPanel::OnRender() {
    if (!isOpen_) {
        return;
    }

    ImGui::Begin("Advanced Lot Plop", &isOpen_, ImGuiWindowFlags_AlwaysAutoResize);
    if (!isOpen_) {
        ImGui::End();
        return;
    }

    const auto& lots = director_->GetLots();

    if (lots.empty()) {
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

void LotPlopPanel::RenderLotsTab_() {
    const auto& lots = director_->GetLots();

    // Render filter UI
    RenderFilterUI_();

    ImGui::Separator();

    // Get filtered and sorted lots
    const auto filteredLots = filterHelper_.ApplyFiltersAndSort(lots, director_->GetFavoriteLotIds());

    // Lazy-load icons for visible lots (prioritize filtered results)
    if (!texturesLoaded_ && imguiService_) {
        size_t loaded = 0;
        for (const Lot* lot : filteredLots) {
            if (loaded >= kMaxIconsToLoadPerFrame) break;
            if (lot->building.thumbnail.has_value() &&
                !iconCache_.contains(lot->building.instanceId.value())) {
                LoadIconTexture_(lot->building.instanceId.value(), lot->building);
                loaded++;
            }
        }
        if (loaded == 0) {
            texturesLoaded_ = true;
        }
    }

    // Show count
    ImGui::Text("Showing %zu of %zu lots", filteredLots.size(), lots.size());

    // Render table with pre-filtered data
    RenderTable_(filteredLots);
}

void LotPlopPanel::RenderFilterUI_() {
    // Search bar - use local buffer for ImGui compatibility
    static char searchBuf[256] = {};
    if (filterHelper_.searchBuffer != searchBuf) {
        std::strncpy(searchBuf, filterHelper_.searchBuffer.c_str(), sizeof(searchBuf) - 1);
        searchBuf[sizeof(searchBuf) - 1] = '\0';
    }

    // Row 1: Search bar (full width)
    ImGui::SetNextItemWidth(-1);  // Full width
    if (ImGui::InputTextWithHint("##Search", "Search lots and buildings...", searchBuf, sizeof(searchBuf))) {
        filterHelper_.searchBuffer = searchBuf;
    }

    // Row 2: Zone type and Wealth
    const char* zoneTypes[] = {
        "Any zone", "Residential (R)", "Commercial (C)", "Industrial (I)", "Plopped", "None", "Other"
    };
    int currentZone = filterHelper_.selectedZoneType.has_value() ?
        (filterHelper_.selectedZoneType.value() + 1) : 0;
    ImGui::SetNextItemWidth(UI::kDropdownWidth);
    if (ImGui::Combo("##ZoneType", &currentZone, zoneTypes, 7)) {
        if (currentZone == 0) {
            filterHelper_.selectedZoneType.reset();  // Any zone
        } else {
            filterHelper_.selectedZoneType = static_cast<uint8_t>(currentZone - 1);
        }
    }

    ImGui::SameLine();

    const char* wealthOptions[] = {"Any wealth", "$", "$$", "$$$"};
    int currentWealth = filterHelper_.selectedWealthType.has_value() ?
        filterHelper_.selectedWealthType.value() : 0;
    ImGui::SetNextItemWidth(UI::kDropdownWidth);
    if (ImGui::Combo("##Wealth", &currentWealth, wealthOptions, 4)) {
        if (currentWealth == 0) {
            filterHelper_.selectedWealthType.reset();  // Any
        } else {
            filterHelper_.selectedWealthType = static_cast<uint8_t>(currentWealth);
        }
    }

    ImGui::SameLine();

    // Growth stage filter dropdown (0-15 or 255 for ploppables)
    const char* growthStages[] = {"Any stage", "Plopped (255)", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15"};
    int currentGrowthStageIndex = 0;
    if (filterHelper_.selectedGrowthStage.has_value()) {
        uint8_t val = filterHelper_.selectedGrowthStage.value();
        if (val == 255) {
            currentGrowthStageIndex = 1;  // Plopped
        } else if (val <= 15) {
            currentGrowthStageIndex = val + 2;  // 0-15 mapped to indices 2-17
        }
    }
    ImGui::SetNextItemWidth(UI::kDropdownWidth);
    if (ImGui::Combo("##GrowthStage", &currentGrowthStageIndex, growthStages, 18)) {
        if (currentGrowthStageIndex == 0) {
            filterHelper_.selectedGrowthStage.reset();  // Any
        } else if (currentGrowthStageIndex == 1) {
            filterHelper_.selectedGrowthStage = 255;  // Plopped
        } else {
            filterHelper_.selectedGrowthStage = static_cast<uint8_t>(currentGrowthStageIndex - 2);  // 0-15
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
        } else {
            // Leaf node - show checkbox
            bool isSelected = filterHelper_.selectedOccupantGroups.contains(og.id);
            if (ImGui::Checkbox(og.name.data(), &isSelected)) {
                if (isSelected) {
                    filterHelper_.selectedOccupantGroups.insert(og.id);
                } else {
                    filterHelper_.selectedOccupantGroups.erase(og.id);
                }
            }
        }
    };

    // Render as collapsible section (collapsed by default)
    if (ImGui::CollapsingHeader("Occupant Groups")) {
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 12.0f);  // More compact indentation
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

void LotPlopPanel::RenderTable_() {
    const auto& lots = director_->GetLots();
    const auto filteredLots = filterHelper_.ApplyFiltersAndSort(lots, director_->GetFavoriteLotIds());
    RenderTable_(filteredLots);
}

void LotPlopPanel::RenderTable_(const std::vector<const Lot*>& filteredLots) {
    if (ImGui::BeginTable("LotsTable", 5, ImGuiTableFlags_Borders |
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0, UI::kTableHeight))) {
        ImGui::TableSetupColumn("Fav", ImGuiTableColumnFlags_WidthFixed, UI::kFavColumnWidth);
        ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed, UI::kIconColumnWidth);
        ImGui::TableSetupColumn("Building / Lot", ImGuiTableColumnFlags_WidthFixed, UI::kNameColumnWidth);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, UI::kSizeColumnWidth);
        ImGui::TableSetupColumn("Plop", ImGuiTableColumnFlags_WidthFixed, UI::kPlopColumnWidth);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(filteredLots.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                const Lot* lot = filteredLots[row];
                ImGui::TableNextRow();

                // Star button
                ImGui::TableNextColumn();
                RenderFavButton_(*lot);

                // Icon
                ImGui::TableNextColumn();
                auto it = iconCache_.find(lot->building.instanceId.value());
                if (it != iconCache_.end()) {
                    void* texId = it->second.GetID();
                    if (texId) {
                        ImGui::Image(texId, ImVec2(UI::kIconSize, UI::kIconSize));
                    }
                    else {
                        ImGui::Dummy(ImVec2(UI::kIconSize, UI::kIconSize));
                    }
                }
                else {
                    ImGui::Dummy(ImVec2(UI::kIconSize, UI::kIconSize));
                }

                // Building + Lot Name
                ImGui::TableNextColumn();
                ImGui::BeginGroup();
                ImGui::TextUnformatted(lot->building.name.c_str());
                ImGui::TextDisabled("%s", lot->name.c_str());
                ImGui::EndGroup();
                if (!lot->building.description.empty() && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", lot->building.description.c_str());
                }

                // Size
                ImGui::TableNextColumn();
                ImGui::Text("%dx%d", lot->sizeX, lot->sizeZ);

                // Plop Button
                ImGui::TableNextColumn();
                std::string buttonLabel = "Plop##" +
                    std::to_string(lot->instanceId.value());
                if (ImGui::Button(buttonLabel.c_str())) {
                    director_->TriggerLotPlop(lot->instanceId.value());
                }
            }
        }

        ImGui::EndTable();
    }
}

void LotPlopPanel::RenderFavButton_(const Lot& lot) const {
    const bool isFavorite = director_->IsFavorite(lot.instanceId.value());
    const char* label = isFavorite ? "Y" : "N";

    const std::string buttonLabel = std::string(label) + "##fav" + std::to_string(lot.instanceId.value());
    if (ImGui::Button(buttonLabel.c_str())) {
        director_->ToggleFavorite(lot.instanceId.value());
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(isFavorite ? "Remove from favorites" : "Add to favorites");
    }
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
