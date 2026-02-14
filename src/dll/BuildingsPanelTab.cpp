#include "BuildingsPanelTab.hpp"

#include <regex>

#include "OccupantGroups.hpp"
#include "Utils.hpp"
#include "jsoncons/staj_event.hpp"
#include "rfl/visit.hpp"

const char* BuildingsPanelTab::GetTabName() const {
    return "Buildings & Lots";
}

void BuildingsPanelTab::OnRender() {
    const auto& buildings = director_->GetBuildings();

    if (buildings.empty()) {
        ImGui::TextUnformatted("No buildings loaded. Please ensure lot_configs.cbor exists in the Plugins directory.");
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

ImGuiTexture BuildingsPanelTab::LoadBuildingTexture_(uint64_t buildingKey) {
    ImGuiTexture texture;

    if (!imguiService_) {
        spdlog::warn("Could not load building thumbnail: imguiService_ is null");
        return texture;
    }

    const auto& buildingsById = director_->GetBuildingsById();
    if (!buildingsById.contains(buildingKey)) {
        spdlog::warn("Could not find building with key 0x{:016X} in buildings map", buildingKey);
        return texture;
    }
    const auto& building = buildingsById.at(buildingKey);
    if (!building.thumbnail.has_value()) {
        spdlog::warn("Building with key 0x{:016X} has no thumbnail", buildingKey);
    }

    const auto& thumbnail = building.thumbnail.value();

    rfl::visit([&](const auto& variant) {
        const auto& data = variant.data;
        const uint32_t width = variant.width;
        const uint32_t height = variant.height;

        if (data.empty() || width == 0 || height == 0) {
            return;
        }

        const size_t expectedSize = static_cast<size_t>(width) * height * 4;
        if (data.size() != expectedSize) {
            spdlog::warn("Building icon data size mismatch for key 0x{:016X}: expected {}, got {}",
                         buildingKey, expectedSize, data.size());
            return;
        }

        texture.Create(imguiService_, width, height, data.data());
    }, thumbnail);

    return texture;
}

void BuildingsPanelTab::RenderFilterUI_() {
    static char searchBuf[256] = {};
    if (filter_.searchBuffer != searchBuf) {
        std::strncpy(searchBuf, filter_.searchBuffer.c_str(), sizeof(searchBuf) - 1);
        searchBuf[sizeof(searchBuf) - 1] = '\0';
    }

    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##SearchBuildings", "Search buildings...", searchBuf, sizeof(searchBuf))) {
        filter_.searchBuffer = searchBuf;
    }

    // Zone type filter
    const char* zoneTypes[] = {
        "Any zone", "Residential (R)", "Commercial (C)", "Industrial (I)", "Plopped", "None", "Other"
    };
    int currentZone = filter_.selectedZoneType.has_value() ? (filter_.selectedZoneType.value() + 1) : 0;
    ImGui::SetNextItemWidth(UI::kDropdownWidth);
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
    ImGui::SetNextItemWidth(UI::kDropdownWidth);
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
    ImGui::SetNextItemWidth(UI::kDropdownWidth);
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
    ImGui::Checkbox("Favorites only", &filter_.favoritesOnly);

    // Size filters
    ImGui::Text("Width:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(UI::kSlider2Width);
    if (ImGui::InputInt("##MinSizeX", &filter_.minSizeX, 1, 1)) {
        filter_.minSizeX = std::clamp(filter_.minSizeX, LotSize::kMinSize, LotSize::kMaxSize);
    }
    ImGui::SameLine();
    ImGui::Text("to");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(UI::kSliderWidth);
    if (ImGui::InputInt("##MaxSizeX", &filter_.maxSizeX, 1, 1)) {
        filter_.maxSizeX = std::clamp(filter_.maxSizeX, LotSize::kMinSize, LotSize::kMaxSize);
    }

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    ImGui::Text("Depth:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(UI::kSliderWidth);
    if (ImGui::InputInt("##MinSizeZ", &filter_.minSizeZ, 1, 1)) {
        filter_.minSizeZ = std::clamp(filter_.minSizeZ, LotSize::kMinSize, LotSize::kMaxSize);
    }
    ImGui::SameLine();
    ImGui::Text("to");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(UI::kSliderWidth);
    if (ImGui::InputInt("##MaxSizeZ", &filter_.maxSizeZ, 1, 1)) {
        filter_.maxSizeZ = std::clamp(filter_.maxSizeZ, LotSize::kMinSize, LotSize::kMaxSize);
    }

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    if (ImGui::Button("Clear filters")) {
        filter_.ResetFilters();
        searchBuf[0] = '\0';
    }

    ImGui::Separator();
    RenderOccupantGroupFilter_();
}

void BuildingsPanelTab::RenderBuildingsTable_(const float tableHeight) {
    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_Sortable |
        ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTable("BuildingsTable", 4, tableFlags, ImVec2(0, tableHeight))) {
        ImGui::TableSetupColumn("Thumbnail",
                                ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                UI::kIconColumnWidth);
        ImGui::TableSetupColumn("Name",
                                ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_DefaultSort |
                                ImGuiTableColumnFlags_PreferSortAscending);
        ImGui::TableSetupColumn("Description",
                                ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Lots",
                                ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                40.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
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
        constexpr float rowHeight = UI::kIconSize + 8.0f;
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(filteredBuildings_.size()), rowHeight);

        while (clipper.Step()) {
            // Request texture loads for visible + margin items
            const int prefetchStart = std::max(0, clipper.DisplayStart - Cache::kPrefetchMargin);
            const int prefetchEnd = std::min(static_cast<int>(filteredBuildings_.size()),
                                             clipper.DisplayEnd + Cache::kPrefetchMargin);

            for (int i = prefetchStart; i < prefetchEnd; ++i) {
                const auto& building = filteredBuildings_[i];
                if (building->thumbnail.has_value()) {
                    thumbnailCache_.Request(MakeGIKey(building->groupId.value(), building->instanceId.value()));
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
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoHide);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, UI::kSizeColumnWidth);
        ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthFixed, UI::kStageColumnWidth);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                                UI::kActionColumnWidth);
        ImGui::TableSetupScrollFreeze(0, 1);
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
    constexpr float rowHeight = UI::kIconSize + 8.0f;
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
            director_->TriggerLotPlop(building.lots[0].instanceId.value());
        }
    }
    ImGui::SameLine();
    auto thumbTextureId = thumbnailCache_.Get(key);
    if (thumbTextureId.has_value() && *thumbTextureId != nullptr) {
        ImGui::Image(*thumbTextureId, ImVec2(UI::kIconSize, UI::kIconSize));
    } else {
        ImGui::Dummy(ImVec2(UI::kIconSize, UI::kIconSize));
    }

    // Name column
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(building.name.c_str());

    // Description column
    ImGui::TableNextColumn();
    if (!building.description.empty()) {
        const std::regex multiNewLinesRe("\n+");
        const auto res = std::regex_replace(building.description, multiNewLinesRe, "\n");
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
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 12.0f);
        ImGui::Text("%s", preview.c_str());

        if (ImGui::BeginChild("##OGTree", ImVec2(0, 150), true)) {
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

void BuildingsPanelTab::RenderFavButton_(const uint32_t lotInstanceId) const {
    const bool isFavorite = director_->IsFavorite(lotInstanceId);
    const char* label = isFavorite ? "Unstar" : "Star";

    if (ImGui::SmallButton(label)) {
        director_->ToggleFavorite(lotInstanceId);
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(isFavorite ? "Remove from favorites" : "Add to favorites");
    }
}

void BuildingsPanelTab::ApplyFilters_() {
    const auto& buildings = director_->GetBuildings();
    const auto& favoriteLots = director_->GetFavoriteLotIds();

    filteredBuildings_.clear();
    filteredBuildings_.reserve(buildings.size());

    for (const auto& building : buildings) {
        // A building passes if any of its lots passes all filters
        bool hasMatchingLot = std::ranges::any_of(building.lots, [&](const Lot& lot) {
            LotView view{&building, &lot};
            return filter_.PassesFilters(view) &&
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
