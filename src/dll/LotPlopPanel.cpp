#include "LotPlopPanel.hpp"

#include "imgui_impl_win32.h"
#include "OccupantGroups.hpp"
#include "rfl/visit.hpp"
#include "spdlog/spdlog.h"

LotPlopPanel::LotPlopPanel(SC4AdvancedLotPlopDirector* director, cIGZImGuiService* imguiService)
    : director_(director), imguiService_(imguiService) {}

void LotPlopPanel::OnRender() {
    ImGui::Begin("Advanced Lot Plop", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

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

    ImGui::SetNextItemWidth(UI::kSearchBarWidth);
    if (ImGui::InputTextWithHint("Search", "Search names", searchBuf, sizeof(searchBuf))) {
        filterHelper_.searchBuffer = searchBuf;
    }

    // Size filters
    ImGui::Text("Width:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(UI::kSliderWidth);
    ImGui::SliderInt("##MinSizeX", &filterHelper_.minSizeX, LotSize::kMinSize, LotSize::kMaxSize);
    ImGui::SameLine();
    ImGui::Text("to");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(UI::kSliderWidth);
    ImGui::SliderInt("##MaxSizeX", &filterHelper_.maxSizeX, LotSize::kMinSize, LotSize::kMaxSize);

    ImGui::Text("Length:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(UI::kSliderWidth);
    ImGui::SliderInt("##MinSizeZ", &filterHelper_.minSizeZ, LotSize::kMinSize, LotSize::kMaxSize);
    ImGui::SameLine();
    ImGui::Text("to");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(UI::kSliderWidth);
    ImGui::SliderInt("##MaxSizeZ", &filterHelper_.maxSizeZ, LotSize::kMinSize, LotSize::kMaxSize);

    // Occupant Group filter
    RenderOccupantGroupFilter_();

    ImGui::SameLine();

    // Clear Filters button
    if (ImGui::Button("Clear")) {
        filterHelper_.ResetFilters();
    }
}

void LotPlopPanel::RenderOccupantGroupFilter_() {
    // Build preview string
    std::string preview;
    if (filterHelper_.selectedOccupantGroups.empty()) {
        preview = "All OGs";
    }
    else {
        preview = std::to_string(filterHelper_.selectedOccupantGroups.size()) + " selected";
    }

    ImGui::SetNextItemWidth(UI::kDropdownWidth);
    if (ImGui::BeginCombo("OGs", preview.c_str())) {
        for (const auto& [id, name] : COMMON_OCCUPANT_GROUPS) {
            bool isSelected = filterHelper_.selectedOccupantGroups.contains(id);
            if (ImGui::Checkbox(name.data(), &isSelected)) {
                if (isSelected) {
                    filterHelper_.selectedOccupantGroups.insert(id);
                }
                else {
                    filterHelper_.selectedOccupantGroups.erase(id);
                }
            }
        }
        ImGui::EndCombo();
    }
}

void LotPlopPanel::RenderTable_() {
    const auto& lots = director_->GetLots();
    const auto filteredLots = filterHelper_.ApplyFiltersAndSort(lots, director_->GetFavoriteLotIds());
    RenderTable_(filteredLots);
}

void LotPlopPanel::RenderTable_(const std::vector<const Lot*>& filteredLots) {
    if (ImGui::BeginTable("LotsTable", 7, ImGuiTableFlags_Borders |
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0, UI::kTableHeight))) {
        ImGui::TableSetupColumn("Fav", ImGuiTableColumnFlags_WidthFixed, UI::kStarColumnWidth);
        ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed, UI::kIconColumnWidth);
        ImGui::TableSetupColumn("Lot name", ImGuiTableColumnFlags_WidthFixed, UI::kLotNameColumnWidth);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, UI::kSizeColumnWidth);
        ImGui::TableSetupColumn("Growth", ImGuiTableColumnFlags_WidthFixed, UI::kGrowthColumnWidth);
        ImGui::TableSetupColumn("Building name", ImGuiTableColumnFlags_WidthFixed, UI::kBuildingNameColumnWidth);
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
                        ImGui::Image(texId, ImVec2(32, 32));
                    }
                    else {
                        ImGui::Dummy(ImVec2(32, 32));
                    }
                }
                else {
                    ImGui::Dummy(ImVec2(32, 32));
                }

                // Lot Name
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(lot->name.c_str());

                // Size
                ImGui::TableNextColumn();
                ImGui::Text("%dx%d", lot->sizeX, lot->sizeZ);

                // Growth Stage
                ImGui::TableNextColumn();
                ImGui::Text("%u", lot->growthStage);

                // Building Name
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(lot->building.name.c_str());

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
