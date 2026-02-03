#include "PropPanelTab.hpp"

#include <cstring>
#include "Constants.hpp"
#include "rfl/visit.hpp"
#include "spdlog/spdlog.h"

const char* PropPanelTab::GetTabName() const {
    return "Props";
}

void PropPanelTab::OnRender() {
    const auto& props = director_->GetProps();

    if (props.empty()) {
        ImGui::TextUnformatted("No props loaded. Please ensure props.cbor exists in the Plugins directory.");
        return;
    }

    if (iconCache_.empty()) {
        iconCache_.reserve(props.size());
    }

    RenderFilterUI_();

    ImGui::Separator();

    std::vector<PropView> propViews;
    propViews.reserve(props.size());
    for (const auto& prop : props) {
        propViews.push_back(PropView{&prop});
    }

    const auto filteredProps = filterHelper_.ApplyFiltersAndSort(
        propViews, director_->GetFavoritePropIds(), sortSpecs_);

    ImGui::Text("Showing %zu of %zu props", filteredProps.size(), propViews.size());
    if (director_->IsPropPainting()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop painting")) {
            director_->StopPropPainting();
        }
    }

    // Table in scrollable child region so filters stay visible
    if (ImGui::BeginChild("PropTableRegion", ImVec2(0, 0), false)) {
        RenderTableInternal_(filteredProps, director_->GetFavoritePropIds());
    }
    ImGui::EndChild();

    RenderRotationModal_();
}

void PropPanelTab::OnDeviceReset(const uint32_t deviceGeneration) {
    if (deviceGeneration != lastDeviceGeneration_) {
        iconCache_.clear();
        texturesLoaded_ = false;
        lastDeviceGeneration_ = deviceGeneration;
    }
}

void PropPanelTab::LoadIconTexture_(const uint64_t propKey, const Prop& prop) {
    if (!imguiService_ || !prop.thumbnail.has_value()) {
        return;
    }

    if (iconCache_.contains(propKey)) {
        return;
    }

    const auto& thumbnail = prop.thumbnail.value();

    rfl::visit([&](const auto& variant) {
        const auto& data = variant.data;
        const uint32_t width = variant.width;
        const uint32_t height = variant.height;

        if (data.empty() || width == 0 || height == 0) {
            return;
        }

        const size_t expectedSize = static_cast<size_t>(width) * height * 4;
        if (data.size() != expectedSize) {
            spdlog::warn("Prop icon data size mismatch for 0x{:08X}/0x{:08X}: expected {}, got {}",
                         prop.groupId.value(), prop.instanceId.value(), expectedSize, data.size());
            return;
        }

        ImGuiTexture texture;
        if (texture.Create(imguiService_, width, height, data.data())) {
            iconCache_.emplace(propKey, std::move(texture));
        }
        else {
            spdlog::warn("Failed to create prop texture for 0x{:08X}/0x{:08X}",
                         prop.groupId.value(), prop.instanceId.value());
        }
    }, thumbnail);
}

void PropPanelTab::RenderFilterUI_() {
    static char searchBuf[256] = {};
    if (filterHelper_.searchBuffer != searchBuf) {
        std::strncpy(searchBuf, filterHelper_.searchBuffer.c_str(), sizeof(searchBuf) - 1);
        searchBuf[sizeof(searchBuf) - 1] = '\0';
    }

    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##SearchProps", "Search props...", searchBuf, sizeof(searchBuf))) {
        filterHelper_.searchBuffer = searchBuf;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Favorites only", &filterHelper_.favoritesOnly);

    ImGui::Text("Width:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(UI::kSlider2Width);
    ImGui::SliderFloat2("##PropWidth", filterHelper_.propWidth, PropSize::kMinSize, PropSize::kMaxSize,
                        UI::kMeterFloatFormat);
    ImGui::SameLine();
    ImGui::Text("Height:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(UI::kSlider2Width);
    ImGui::SliderFloat2("##PropHeight", filterHelper_.propHeight, PropSize::kMinSize, PropSize::kMaxSize,
                        UI::kMeterFloatFormat);
    ImGui::SameLine();
    ImGui::Text("Depth:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(UI::kSlider2Width);
    ImGui::SliderFloat2("##PropDepth", filterHelper_.propDepth, PropSize::kMinSize, PropSize::kMaxSize,
                        UI::kMeterFloatFormat);

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    if (ImGui::Button("Clear filters")) {
        filterHelper_.ResetFilters();
    }
}

void PropPanelTab::RenderTable_() {
    const auto& props = director_->GetProps();
    std::vector<PropView> propViews;
    propViews.reserve(props.size());
    for (const auto& prop : props) {
        propViews.push_back(PropView{&prop});
    }

    const auto filteredProps = filterHelper_.ApplyFiltersAndSort(
        propViews, director_->GetFavoritePropIds(), sortSpecs_);

    RenderTableInternal_(filteredProps, director_->GetFavoritePropIds());
}

void PropPanelTab::RenderTableInternal_(const std::vector<PropView>& filteredProps,
                                        const std::unordered_set<uint64_t>& favorites) {
    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti;

    if (ImGui::BeginTable("PropsTable", 4, tableFlags, ImVec2(0, 0))) {
        ImGui::TableSetupColumn("Thumbnail",
                                ImGuiTableColumnFlags_WidthFixed |
                                ImGuiTableColumnFlags_NoSort,
                                UI::kIconColumnWidth);
        ImGui::TableSetupColumn("Name",
                                ImGuiTableColumnFlags_NoHide |
                                ImGuiTableColumnFlags_DefaultSort |
                                ImGuiTableColumnFlags_PreferSortAscending);
        ImGui::TableSetupColumn("Size (m)",
                                ImGuiTableColumnFlags_WidthFixed |
                                ImGuiTableColumnFlags_PreferSortAscending,
                                UI::kSizeColumnWidth * 1.5);
        ImGui::TableSetupColumn("Action",
                                ImGuiTableColumnFlags_WidthFixed |
                                ImGuiTableColumnFlags_NoSort,
                                UI::kActionColumnWidth);

        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsCount > 0) {
            std::vector<PropFilterHelper::SortSpec> newSpecs;
            newSpecs.reserve(specs->SpecsCount);
            for (auto i = 0; i < specs->SpecsCount; ++i) {
                const auto& s = specs->Specs[i];
                switch (s.ColumnIndex) {
                case 1:
                    newSpecs.push_back({
                        PropFilterHelper::SortColumn::Name,
                        s.SortDirection == ImGuiSortDirection_Descending
                    });
                    break;
                case 2:
                    newSpecs.push_back({
                        PropFilterHelper::SortColumn::Size,
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

        if (!texturesLoaded_ && imguiService_) {
            size_t loaded = 0;
            for (const auto& view : filteredProps) {
                if (loaded >= kMaxIconsToLoadPerFrame) break;
                const auto& prop = *view.prop;
                const uint64_t key = (static_cast<uint64_t>(prop.groupId.value()) << 32) | prop.instanceId.value();
                if (prop.thumbnail.has_value() && !iconCache_.contains(key)) {
                    LoadIconTexture_(key, prop);
                    loaded++;
                }
            }
            if (loaded == 0) {
                texturesLoaded_ = true;
            }
        }

        for (size_t rowIdx = 0; rowIdx < filteredProps.size(); ++rowIdx) {
            const auto& prop = *filteredProps[rowIdx].prop;
            const uint64_t key = (static_cast<uint64_t>(prop.groupId.value()) << 32) | prop.instanceId.value();

            ImGui::PushID(static_cast<int>(rowIdx));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (iconCache_.contains(key)) {
                ImGui::Image(iconCache_[key].GetID(), ImVec2(UI::kIconSize, UI::kIconSize));
            }
            else {
                ImGui::Dummy(ImVec2(UI::kIconSize, UI::kIconSize));
            }
            ImGui::TableNextColumn();
            if (prop.visibleName.empty()) {
                ImGui::TextUnformatted(prop.exemplarName.c_str());
            } else {
                ImGui::TextUnformatted(prop.visibleName.c_str());
                ImGui::TextDisabled("%s", prop.exemplarName.c_str());
            }

            ImGui::TableNextColumn();
            ImGui::Text("%.1f x %.1f x %.1f", prop.width, prop.height, prop.depth);
            ImGui::TableNextColumn();
            if (ImGui::Button("Paint")) {
                if (director_->IsPropPainting() &&
                    director_->SwitchPropPaintingTarget(prop.instanceId.value(), prop.visibleName)) {
                    // Reuse current paint mode and rotation; no modal.
                }
                else {
                    pendingPaint_.propId = prop.instanceId.value();
                    pendingPaint_.propName = prop.visibleName;
                    pendingPaint_.settings.mode = PropPaintMode::Direct;
                    pendingPaint_.settings.rotation = 0;
                    pendingPaint_.open = true;
                }
            }
            ImGui::SameLine();
            RenderFavButton_(prop);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void PropPanelTab::RenderFavButton_(const Prop& prop) const {
    const bool isFavorite = director_->IsPropFavorite(prop.groupId.value(), prop.instanceId.value());
    if (ImGui::Button(isFavorite ? "Unstar" : "Star")) {
        director_->TogglePropFavorite(prop.groupId.value(), prop.instanceId.value());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(isFavorite ? "Remove from favorites" : "Add to favorites");
    }
}

void PropPanelTab::RenderRotationModal_() {
    if (pendingPaint_.open) {
        ImGui::OpenPopup("Prop Paint Options");
        pendingPaint_.open = false;
    }

    if (ImGui::BeginPopupModal("Prop Paint Options", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Prop: %s", pendingPaint_.propName.c_str());
        ImGui::Separator();

        ImGui::TextUnformatted("Mode");
        int mode = static_cast<int>(pendingPaint_.settings.mode);
        ImGui::RadioButton("Direct paint", &mode, static_cast<int>(PropPaintMode::Direct));
#ifdef _DEBUG
        ImGui::RadioButton("Paint along line", &mode, static_cast<int>(PropPaintMode::Line));
        ImGui::RadioButton("Paint inside polygon", &mode, static_cast<int>(PropPaintMode::Polygon));
#endif
        pendingPaint_.settings.mode = static_cast<PropPaintMode>(mode);

        ImGui::Separator();
        ImGui::TextUnformatted("Rotation");
        int rotation = pendingPaint_.settings.rotation;
        ImGui::RadioButton("0 deg", &rotation, 0); ImGui::SameLine();
        ImGui::RadioButton("90 deg", &rotation, 1); ImGui::SameLine();
        ImGui::RadioButton("180 deg", &rotation, 2); ImGui::SameLine();
        ImGui::RadioButton("270 deg", &rotation, 3);
        pendingPaint_.settings.rotation = rotation;

        if (pendingPaint_.settings.mode == PropPaintMode::Line) {
            ImGui::Separator();
            ImGui::SliderFloat("Spacing (m)", &pendingPaint_.settings.spacingMeters, 0.5f, 50.0f, "%.1f");
        }
        else if (pendingPaint_.settings.mode == PropPaintMode::Polygon) {
            ImGui::Separator();
            ImGui::SliderFloat("Density (/100 m^2)", &pendingPaint_.settings.densityPer100Sqm, 0.1f, 20.0f, "%.1f");
        }

        const bool canStart = pendingPaint_.settings.mode == PropPaintMode::Direct;
        if (!canStart) {
            ImGui::TextDisabled("Line/polygon modes are not implemented yet.");
        }

        if (ImGui::Button("Start") && canStart) {
            director_->StartPropPainting(pendingPaint_.propId, pendingPaint_.settings, pendingPaint_.propName);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}
