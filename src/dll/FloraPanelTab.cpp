#include "FloraPanelTab.hpp"

#include <cstdio>
#include <cstring>

#include "Constants.hpp"
#include "SC4PlopAndPaintDirector.hpp"
#include "Utils.hpp"
#include "utils/Logger.h"

FloraPanelTab::FloraPanelTab(SC4PlopAndPaintDirector* director,
                               FloraRepository* flora,
                               FavoritesRepository* favorites,
                               cIGZImGuiService* imguiService)
    : PanelTab(director, nullptr, nullptr, favorites, imguiService)
    , flora_(flora) {
    pendingPaint_.settings.showGrid = director->GetDefaultShowGridOverlay();
    pendingPaint_.settings.snapPointsToGrid = director->GetDefaultSnapPointsToGrid();
    pendingPaint_.settings.snapPlacementsToGrid = director->GetDefaultSnapPlacementsToGrid();
    pendingPaint_.settings.gridStepMeters = director->GetDefaultGridStepMeters();
    pendingPaint_.settings.previewMode = director->GetDefaultPropPreviewMode();
}

const char* FloraPanelTab::GetTabName() const {
    return "Flora";
}

void FloraPanelTab::OnRender() {
    if (!flora_) {
        ImGui::TextUnformatted("Flora repository not available.");
        return;
    }

    if (flora_->GetFloraItems().empty()) {
        ImGui::TextUnformatted("No flora loaded. Please ensure flora.cbor exists in the Plugins directory.");
        return;
    }

    RenderFilterUI_();
    ImGui::Separator();

    ImGui::Text("Flora items: %zu", flora_->GetFloraItems().size());
    if (director_->IsFloraPainting()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop painting")) {
            director_->StopFloraPainting();
        }
    }

    if (ImGui::BeginChild("FloraTableRegion", ImVec2(0, 0), false)) {
        RenderIndividualFloraTable_();

        if (!flora_->GetFloraGroups().empty()) {
            ImGui::Spacing();
            ImGui::SeparatorText("Flora Groups (multi-stage MMPs)");
            ImGui::Spacing();
            RenderGroupsSection_();
        }
    }
    ImGui::EndChild();

    RenderPaintModal_();
}

void FloraPanelTab::OnDeviceReset(const uint32_t deviceGeneration) {
    if (deviceGeneration != lastDeviceGeneration_) {
        thumbnailCache_.OnDeviceReset();
        lastDeviceGeneration_ = deviceGeneration;
    }
}

ImGuiTexture FloraPanelTab::LoadFloraTexture_(const uint64_t key) {
    ImGuiTexture texture;
    if (!imguiService_) {
        return texture;
    }
    auto data = flora_->GetFloraThumbnailStore().LoadThumbnail(key);
    if (!data.has_value()) {
        return texture;
    }
    texture.Create(imguiService_, data->width, data->height, data->rgba.data());
    return texture;
}

void FloraPanelTab::RenderFilterUI_() {
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##SearchFlora", "Search flora...", searchBuf_, sizeof(searchBuf_))) {
        // searchBuf_ is updated in-place
    }
    ImGui::Checkbox("Favorites only", &favoritesOnly_);
}

void FloraPanelTab::RenderIndividualFloraTable_() {
    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBody |
        ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY;

    if (!ImGui::BeginTable("FloraTable", 3, tableFlags, ImVec2(0, 0))) {
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Thumb", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                            UI::iconColumnWidth());
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_DefaultSort |
                            ImGuiTableColumnFlags_PreferSortAscending);
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
                            UI::actionColumnWidth());
    ImGui::TableHeadersRow();

    const auto& items = flora_->GetFloraItems();
    const std::string_view searchStr = searchBuf_;

    const float rowHeight = UI::iconRowHeight();
    ImGuiListClipper clipper;

    // Build filtered list
    std::vector<const Flora*> filtered;
    filtered.reserve(items.size());
    for (const auto& f : items) {
        if (!searchStr.empty()) {
            const bool nameMatch = ContainsCaseInsensitive(f.visibleName, searchStr) ||
                                   ContainsCaseInsensitive(f.exemplarName, searchStr);
            if (!nameMatch) continue;
        }
        filtered.push_back(&f);
    }

    clipper.Begin(static_cast<int>(filtered.size()), rowHeight);
    while (clipper.Step()) {
        const int prefetchStart = std::max(0, clipper.DisplayStart - Cache::kPrefetchMargin);
        const int prefetchEnd = std::min(static_cast<int>(filtered.size()), clipper.DisplayEnd + Cache::kPrefetchMargin);
        for (int i = prefetchStart; i < prefetchEnd; ++i) {
            const auto* fp = filtered[i];
            const uint64_t key = MakeGIKey(fp->groupId.value(), fp->instanceId.value());
            if (flora_->GetFloraThumbnailStore().HasThumbnail(key)) {
                thumbnailCache_.Request(key);
            }
        }

        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& f = *filtered[i];
            const uint64_t key = MakeGIKey(f.groupId.value(), f.instanceId.value());

            ImGui::PushID(static_cast<int>(key));
            ImGui::TableNextRow(0, rowHeight);

            // Thumbnail
            ImGui::TableNextColumn();
            ImGui::Selectable("##row", false,
                              ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                              ImVec2(0, rowHeight));
            ImGui::SameLine();
            auto thumbId = thumbnailCache_.Get(key);
            if (thumbId.has_value() && *thumbId != nullptr) {
                ImGui::Image(*thumbId, ImVec2(UI::kIconSize, UI::kIconSize));
            }
            else {
                ImGui::Dummy(ImVec2(UI::kIconSize, UI::kIconSize));
            }

            // Name
            ImGui::TableNextColumn();
            const char* displayName = f.visibleName.empty() ? f.exemplarName.c_str() : f.visibleName.c_str();
            ImGui::TextUnformatted(displayName);
            if (!f.visibleName.empty() && !f.exemplarName.empty()) {
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::TextDisabled("%s", f.exemplarName.c_str());
            }
            if (f.clusterNextType.has_value()) {
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::TextDisabled("[chain]");
            }

            // Action
            ImGui::TableNextColumn();
            if (ImGui::Button("Paint")) {
                pendingPaint_.typeId = f.instanceId.value();
                pendingPaint_.name   = displayName;
                pendingPaint_.settings.activePalette.clear();
                pendingPaint_.isGroup = false;
                pendingPaint_.open   = true;
            }

            ImGui::PopID();
        }
    }

    thumbnailCache_.ProcessLoadQueue([this](const uint64_t key) {
        return LoadFloraTexture_(key);
    });

    ImGui::EndTable();
}

void FloraPanelTab::RenderGroupsSection_() {
    const auto& groups = flora_->GetFloraGroups();
    if (groups.empty()) {
        return;
    }

    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBody;

    if (!ImGui::BeginTable("FloraGroupsTable", 3, tableFlags)) {
        return;
    }

    ImGui::TableSetupColumn("Group Name", ImGuiTableColumnFlags_NoHide);
    ImGui::TableSetupColumn("Stages", ImGuiTableColumnFlags_WidthFixed, 4.0f * ImGui::GetFontSize());
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, UI::actionColumnWidth());
    ImGui::TableHeadersRow();

    for (size_t gi = 0; gi < groups.size(); ++gi) {
        const auto& group = groups[gi];
        ImGui::PushID(static_cast<int>(gi));
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(group.name.c_str());

        ImGui::TableNextColumn();
        ImGui::Text("%zu", group.entries.size());

        ImGui::TableNextColumn();
        if (ImGui::Button("Paint")) {
            // Use first entry's typeId as fallback; palette covers all stages
            pendingPaint_.typeId = group.entries.empty() ? 0 : group.entries[0].propID.value();
            pendingPaint_.name   = group.name;
            pendingPaint_.settings.activePalette = group.entries;
            pendingPaint_.isGroup = true;
            pendingPaint_.open   = true;
        }

        ImGui::PopID();
    }

    ImGui::EndTable();
}

void FloraPanelTab::RenderPaintModal_() {
    if (pendingPaint_.open) {
        ImGui::OpenPopup("Flora Painter");
        pendingPaint_.open = false;
    }

    if (!ImGui::BeginPopupModal("Flora Painter", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::Text("Flora: %s", pendingPaint_.name.c_str());
    if (pendingPaint_.isGroup) {
        ImGui::TextDisabled("(multi-stage MMP group - stages placed randomly)");
    }
    ImGui::Separator();

    ImGui::TextUnformatted("Mode");
    int mode = static_cast<int>(pendingPaint_.settings.mode);
    ImGui::RadioButton("Direct paint",        &mode, static_cast<int>(PropPaintMode::Direct));
    ImGui::RadioButton("Paint along line",    &mode, static_cast<int>(PropPaintMode::Line));
    ImGui::RadioButton("Paint inside polygon",&mode, static_cast<int>(PropPaintMode::Polygon));
    pendingPaint_.settings.mode = static_cast<PropPaintMode>(mode);

    ImGui::Separator();
    ImGui::TextUnformatted("Rotation");
    int rotation = pendingPaint_.settings.rotation & 3;
    ImGui::RadioButton("0 deg", &rotation, 0);
    ImGui::SameLine();
    ImGui::RadioButton("90 deg", &rotation, 1);
    ImGui::SameLine();
    ImGui::RadioButton("180 deg", &rotation, 2);
    ImGui::SameLine();
    ImGui::RadioButton("270 deg", &rotation, 3);
    pendingPaint_.settings.rotation = rotation;

    ImGui::Separator();
    ImGui::TextUnformatted("Grid");
    ImGui::Checkbox("Show grid overlay", &pendingPaint_.settings.showGrid);
    ImGui::Checkbox("Snap points to grid", &pendingPaint_.settings.snapPointsToGrid);
    if (pendingPaint_.settings.snapPointsToGrid) {
        ImGui::Checkbox("Also snap placements to grid", &pendingPaint_.settings.snapPlacementsToGrid);
    }
    else {
        pendingPaint_.settings.snapPlacementsToGrid = false;
        ImGui::BeginDisabled();
        bool dummy = false;
        ImGui::Checkbox("Also snap placements to grid", &dummy);
        ImGui::EndDisabled();
    }
    ImGui::SliderFloat("Grid step (m)", &pendingPaint_.settings.gridStepMeters, 1.0f, 16.0f, "%.1f");
    ImGui::SliderFloat("Vertical offset (m)", &pendingPaint_.settings.deltaYMeters, 0.0f, 100.0f, "%.1f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Raises placed flora above the terrain and preview grid.");
    }
    static constexpr const char* kPreviewModeLabels[] = {
        "Outline only",
        "Full flora only",
        "Outline + full flora"
    };
    int previewMode = static_cast<int>(pendingPaint_.settings.previewMode);
    ImGui::Combo("Direct preview", &previewMode, kPreviewModeLabels, IM_ARRAYSIZE(kPreviewModeLabels));
    pendingPaint_.settings.previewMode = static_cast<PropPreviewMode>(previewMode);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Used only for direct paint mode. Line and polygon previews always use the outline overlay.");
    }

    if (pendingPaint_.settings.mode == PropPaintMode::Line) {
        ImGui::Separator();
        ImGui::SliderFloat("Spacing (m)", &pendingPaint_.settings.spacingMeters, 0.5f, 50.0f, "%.1f");
        ImGui::Checkbox("Align to path direction", &pendingPaint_.settings.alignToPath);
        ImGui::Checkbox("Random rotation", &pendingPaint_.settings.randomRotation);
        ImGui::SliderFloat("Lateral jitter (m)", &pendingPaint_.settings.randomOffset, 0.0f, 5.0f, "%.1f");
        ImGui::TextWrapped("Click to add line points. Enter places flora. Backspace removes the last point.");
    }
    else if (pendingPaint_.settings.mode == PropPaintMode::Polygon) {
        ImGui::Separator();
        ImGui::SliderFloat("Density (/100 m^2)", &pendingPaint_.settings.densityPer100Sqm, 0.1f, 10.0f, "%.2f");
        ImGui::SliderFloat("Density variation", &pendingPaint_.settings.densityVariation, 0.0f, 1.0f, "%.2f");
        ImGui::Checkbox("Random rotation", &pendingPaint_.settings.randomRotation);
        ImGui::TextWrapped("Click to add polygon vertices. Enter fills with flora. Backspace removes the last vertex.");
    }
    else {
        ImGui::TextWrapped("Click to place flora directly. Enter commits placed flora.");
    }

    if (ImGui::Button("Start")) {
        ReleaseImGuiInputCapture_();
        director_->StartFloraPainting(pendingPaint_.typeId, pendingPaint_.settings, pendingPaint_.name);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}
