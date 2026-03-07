#include "FloraCollectionsPanelTab.hpp"

#include <algorithm>
#include <cstdio>

#include "../SC4PlopAndPaintDirector.hpp"
#include "../common/BadgeUtils.hpp"
#include "../common/Constants.hpp"
#include "../common/Utils.hpp"

namespace {
    constexpr ImU32 kMultiStageColor = IM_COL32(82, 92, 132, 255);
    constexpr ImU32 kMultiStageHoverColor = IM_COL32(98, 110, 156, 255);

    std::string FormatHexId(const uint32_t value) {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "0x%08X", value);
        return buffer;
    }

    std::string FloraDisplayName(const Flora& flora) {
        if (!flora.visibleName.empty()) {
            return flora.visibleName;
        }
        if (!flora.exemplarName.empty()) {
            return flora.exemplarName;
        }
        return "<unnamed flora>";
    }

    bool MatchesIidFilter(const uint32_t id, std::string_view filter) {
        if (filter.empty()) {
            return true;
        }

        const std::string formatted = FormatHexId(id);
        std::string compact = formatted;
        compact.erase(std::remove(compact.begin(), compact.end(), 'x'), compact.end());
        compact.erase(std::remove(compact.begin(), compact.end(), 'X'), compact.end());

        const std::string normalizedFilter = ToUpperCopy(filter);
        return ToUpperCopy(formatted).contains(normalizedFilter) || ToUpperCopy(compact).contains(normalizedFilter);
    }

    const char* CollectionTypeLabel(const FloraRepository::CollectionType type) {
        switch (type) {
        case FloraRepository::CollectionType::Family:
            return "Family";
        case FloraRepository::CollectionType::MultiStage:
            return "Chain";
        }

        return "?";
    }
}

FloraCollectionsPanelTab::FloraCollectionsPanelTab(SC4PlopAndPaintDirector* director,
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

const char* FloraCollectionsPanelTab::GetTabName() const {
    return "Flora Collections";
}

void FloraCollectionsPanelTab::OnRender() {
    if (!flora_) {
        ImGui::TextUnformatted("Flora repository not available.");
        return;
    }

    const auto& collections = flora_->GetFloraCollections();

    if (collections.empty()) {
        ImGui::TextDisabled("No flora collections available.");
        ImGui::TextWrapped("Collections appear from flora families and multi-stage chains once flora data is loaded.");
        return;
    }

    RenderFilterUI_();
    ImGui::Separator();

    std::vector<size_t> filteredIndices;
    BuildFilteredCollectionIndices_(collections, filteredIndices);
    if (!filteredIndices.empty() &&
        std::ranges::none_of(filteredIndices, [this, &collections](const size_t index) {
            return BuildCollectionKey_(collections[index].type, collections[index].id) == selectedCollectionKey_;
        })) {
        const auto& first = collections[filteredIndices.front()];
        selectedCollectionKey_ = BuildCollectionKey_(first.type, first.id);
    }

    ImGui::Text("Showing %zu of %zu collections", filteredIndices.size(), collections.size());
    if (director_->IsFloraPainting()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop painting")) {
            director_->StopFloraPainting();
        }
    }

    ImGui::Separator();
    if (ImGui::BeginChild("FloraCollectionsTableRegion", ImVec2(0, UI::familyTableHeight()), false)) {
        RenderCollectionsTable_(collections, filteredIndices);
    }
    ImGui::EndChild();

    if (filteredIndices.empty()) {
        ImGui::TextDisabled("No collections match the current filters.");
    }
    else {
        RenderSelectedCollectionPanel_(collections);
    }

    thumbnailCache_.ProcessLoadQueue([this](const uint64_t key) {
        return LoadFloraTexture_(key);
    });
    RenderPaintModal_();
}

void FloraCollectionsPanelTab::OnDeviceReset(const uint32_t deviceGeneration) {
    if (deviceGeneration != lastDeviceGeneration_) {
        thumbnailCache_.OnDeviceReset();
        lastDeviceGeneration_ = deviceGeneration;
    }
}

ImGuiTexture FloraCollectionsPanelTab::LoadFloraTexture_(const uint64_t key) const {
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

void FloraCollectionsPanelTab::RenderFilterUI_() {
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##SearchFloraCollections", "Search collection name...", nameFilterBuf_, sizeof(nameFilterBuf_));

    ImGui::SetNextItemWidth(UI::iidFilterWidth());
    ImGui::InputTextWithHint("##SearchFloraCollectionsByIid", "Filter IID (0x...)", iidFilterBuf_, sizeof(iidFilterBuf_));
    ImGui::SameLine();

    static constexpr const char* kTypeOptions[] = {"All types", "Families", "Multi-stage"};
    ImGui::SetNextItemWidth(UI::dropdownWidth());
    ImGui::Combo("##FloraCollectionType", &typeFilter_, kTypeOptions, IM_ARRAYSIZE(kTypeOptions));

    ImGui::SameLine();
    ImGui::Checkbox("Favorites only", &favoritesOnly_);
    ImGui::SameLine();
    if (ImGui::Button("Clear filters")) {
        nameFilterBuf_[0] = '\0';
        iidFilterBuf_[0] = '\0';
        typeFilter_ = 0;
        favoritesOnly_ = false;
    }
}

void FloraCollectionsPanelTab::BuildFilteredCollectionIndices_(const std::vector<FloraRepository::FloraCollection>& collections,
                                                               std::vector<size_t>& filteredIndices) const {
    filteredIndices.clear();
    filteredIndices.reserve(collections.size());

    const std::string_view nameFilter = nameFilterBuf_;
    const std::string_view iidFilter = iidFilterBuf_;
    const auto& favoriteFloraIds = favorites_->GetFavoriteFloraIds();

    for (size_t i = 0; i < collections.size(); ++i) {
        const auto& collection = collections[i];

        if (typeFilter_ == 1 && collection.type != FloraRepository::CollectionType::Family) {
            continue;
        }
        if (typeFilter_ == 2 && collection.type != FloraRepository::CollectionType::MultiStage) {
            continue;
        }
        if (!ContainsCaseInsensitive(collection.name, nameFilter)) {
            continue;
        }
        if (!MatchesIidFilter(collection.id, iidFilter)) {
            continue;
        }
        if (favoritesOnly_) {
            const bool hasFavoriteMember = std::ranges::any_of(collection.members, [&favoriteFloraIds](const Flora* flora) {
                return flora &&
                    favoriteFloraIds.contains(MakeGIKey(flora->groupId.value(), flora->instanceId.value()));
            });
            if (!hasFavoriteMember) {
                continue;
            }
        }

        filteredIndices.push_back(i);
    }
}

void FloraCollectionsPanelTab::RenderCollectionsTable_(const std::vector<FloraRepository::FloraCollection>& collections,
                                                       const std::vector<size_t>& filteredIndices) {
    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBody |
        ImGuiTableFlags_ScrollY;

    if (!ImGui::BeginTable("FloraCollectionsTable", 5, tableFlags, ImVec2(0, 0))) {
        return;
    }

    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, UI::typeColumnWidth());
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Instance ID", ImGuiTableColumnFlags_WidthFixed, UI::instanceIdColumnWidth());
    ImGui::TableSetupColumn("Items", ImGuiTableColumnFlags_WidthFixed, UI::propsColumnWidth());
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, UI::familyActionColWidth());
    ImGui::TableHeadersRow();
    ImGui::TableSetupScrollFreeze(0, 1);

    for (const size_t collectionIndex : filteredIndices) {
        const auto& collection = collections[collectionIndex];
        const bool selected = BuildCollectionKey_(collection.type, collection.id) == selectedCollectionKey_;

        ImGui::PushID(static_cast<int>(collectionIndex));
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::Selectable("##collectionrow", selected,
                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap |
                              ImGuiSelectableFlags_AllowDoubleClick);
        if (ImGui::IsItemClicked()) {
            selectedCollectionKey_ = BuildCollectionKey_(collection.type, collection.id);
            if (ImGui::IsMouseDoubleClicked(0)) {
                QueuePaintForCollection_(collection);
            }
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(CollectionTypeLabel(collection.type));

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(collection.name.c_str());

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(FormatHexId(collection.id).c_str());

        ImGui::TableNextColumn();
        ImGui::Text("%zu", collection.members.size());

        ImGui::TableNextColumn();
        if (ImGui::SmallButton("Paint")) {
            selectedCollectionKey_ = BuildCollectionKey_(collection.type, collection.id);
            QueuePaintForCollection_(collection);
        }

        ImGui::PopID();
    }

    ImGui::EndTable();
}

void FloraCollectionsPanelTab::RenderSelectedCollectionPanel_(const std::vector<FloraRepository::FloraCollection>& collections) {
    const FloraRepository::FloraCollection* selectedCollection = GetSelectedCollection_(collections);
    if (!selectedCollection) {
        return;
    }

    ImGui::TextDisabled(selectedCollection->type == FloraRepository::CollectionType::Family
                            ? "Read-only flora family"
                            : "Read-only multi-stage collection");
    ImGui::TextUnformatted(selectedCollection->name.c_str());
    ImGui::Separator();
    ImGui::Text("IID: %s", FormatHexId(selectedCollection->id).c_str());
    ImGui::Text("%zu flora items in collection", selectedCollection->members.size());
    ImGui::TextDisabled(selectedCollection->type == FloraRepository::CollectionType::Family
                            ? "Painting picks randomly from the family members."
                            : "Painting picks from the chain stages.");

    if (selectedCollection->members.empty()) {
        ImGui::TextDisabled("No flora in this collection.");
    }
    else if (ImGui::BeginTable("FloraCollectionEntries", 4,
                               ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                               ImVec2(0, UI::familyEntriesHeight()))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##icon", ImGuiTableColumnFlags_WidthFixed, UI::iconColumnWidth());
        ImGui::TableSetupColumn("Flora", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Instance ID", ImGuiTableColumnFlags_WidthFixed, UI::instanceIdColumnWidth());
        ImGui::TableSetupColumn("Fav", ImGuiTableColumnFlags_WidthFixed, UI::favoriteColumnWidth());
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < selectedCollection->members.size(); ++i) {
            const Flora* flora = selectedCollection->members[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow(0, UI::iconRowHeight());

            ImGui::TableNextColumn();
            if (flora) {
                const uint64_t key = MakeGIKey(flora->groupId.value(), flora->instanceId.value());
                if (flora_->GetFloraThumbnailStore().HasThumbnail(key)) {
                    thumbnailCache_.Request(key);
                }
                auto thumbId = thumbnailCache_.Get(key);
                if (thumbId.has_value() && *thumbId != nullptr) {
                    ImGui::Image(*thumbId, ImVec2(UI::kIconSize, UI::kIconSize));
                }
                else {
                    ImGui::Dummy(ImVec2(UI::kIconSize, UI::kIconSize));
                }
            }
            else {
                ImGui::Dummy(ImVec2(UI::kIconSize, UI::kIconSize));
            }

            ImGui::TableNextColumn();
            if (flora) {
                ImGui::TextUnformatted(FloraDisplayName(*flora).c_str());
                const bool renderedPills = RenderFloraPills_(*flora, true);
                if (!flora->visibleName.empty() && !flora->exemplarName.empty()) {
                    if (renderedPills) {
                        ImGui::SameLine(0.0f, 4.0f);
                    }
                    ImGui::TextDisabled("%s", flora->exemplarName.c_str());
                }
            }
            else {
                ImGui::TextDisabled("Missing flora");
            }

            ImGui::TableNextColumn();
            if (flora) {
                ImGui::TextUnformatted(FormatHexId(flora->instanceId.value()).c_str());
            }
            else {
                ImGui::TextDisabled("-");
            }

            ImGui::TableNextColumn();
            if (flora) {
                const std::string idSuffix = std::to_string(i);
                RenderFavoriteButton_(*flora, idSuffix.c_str());
            }
            else {
                ImGui::TextDisabled("-");
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::Separator();
    if (selectedCollection->members.empty()) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Paint collection")) {
        QueuePaintForCollection_(*selectedCollection);
    }
    if (selectedCollection->members.empty()) {
        ImGui::EndDisabled();
    }
}

void FloraCollectionsPanelTab::RenderPaintModal_() {
    if (pendingPaint_.open) {
        ImGui::OpenPopup("Flora Collection Paint Options");
        pendingPaint_.open = false;
    }

    if (!ImGui::BeginPopupModal("Flora Collection Paint Options", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::Text("Collection: %s", pendingPaint_.name.c_str());
    ImGui::TextDisabled("%s", pendingPaint_.detail.c_str());
    ImGui::Separator();

    ImGui::TextUnformatted("Mode");
    int mode = static_cast<int>(pendingPaint_.settings.mode);
    ImGui::RadioButton("Direct paint", &mode, static_cast<int>(PaintMode::Direct));
    ImGui::RadioButton("Paint along line", &mode, static_cast<int>(PaintMode::Line));
    ImGui::RadioButton("Paint inside polygon", &mode, static_cast<int>(PaintMode::Polygon));
    pendingPaint_.settings.mode = static_cast<PaintMode>(mode);

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
    pendingPaint_.settings.previewMode = static_cast<PreviewMode>(previewMode);

    if (pendingPaint_.settings.mode == PaintMode::Line) {
        ImGui::Separator();
        ImGui::SliderFloat("Spacing (m)", &pendingPaint_.settings.spacingMeters, 0.5f, 50.0f, "%.1f");
        ImGui::Checkbox("Align to path direction", &pendingPaint_.settings.alignToPath);
        ImGui::Checkbox("Random rotation", &pendingPaint_.settings.randomRotation);
        ImGui::SliderFloat("Lateral jitter (m)", &pendingPaint_.settings.randomOffset, 0.0f, 5.0f, "%.1f");
        ImGui::TextWrapped("Click to add line points. Enter places flora. Backspace removes the last point.");
    }
    else if (pendingPaint_.settings.mode == PaintMode::Polygon) {
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

void FloraCollectionsPanelTab::QueuePaintForCollection_(const FloraRepository::FloraCollection& collection) {
    pendingPaint_.typeId = collection.palette.empty() ? 0 : collection.palette.front().propID.value();
    pendingPaint_.name = collection.name;
    pendingPaint_.detail = collection.type == FloraRepository::CollectionType::Family
        ? "Randomly picks from the flora family members."
        : "Randomly picks from the multi-stage chain.";
    pendingPaint_.settings.activePalette = collection.palette;
    pendingPaint_.open = true;
}

void FloraCollectionsPanelTab::RenderFavoriteButton_(const Flora& flora, const char* idSuffix) const {
    const bool isFavorite = favorites_->IsFloraFavorite(flora.groupId.value(), flora.instanceId.value());
    const std::string label = std::string(isFavorite ? "Unstar##" : "Star##") + idSuffix;
    if (ImGui::Button(label.c_str())) {
        favorites_->ToggleFloraFavorite(flora.groupId.value(), flora.instanceId.value());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(isFavorite ? "Remove from favorites" : "Add to favorites");
    }
}

bool FloraCollectionsPanelTab::RenderFloraPills_(const Flora& flora, const bool startOnNewLine) const {
    bool renderedAny = false;
    const auto renderPill = [&](const char* label, const ImU32 baseColor, const ImU32 hoverColor) {
        if (!renderedAny) {
            if (!startOnNewLine) {
                ImGui::SameLine(0.0f, 4.0f);
            }
        }
        else {
            ImGui::SameLine(0.0f, 4.0f);
        }
        Badges::RenderPill(label, baseColor, hoverColor);
        renderedAny = true;
    };

    if (!flora.familyIds.empty()) {
        renderPill("Family", Badges::kFamilyColor, Badges::kFamilyHoverColor);
    }
    if (flora.clusterNextType.has_value()) {
        renderPill("Multi-stage", kMultiStageColor, kMultiStageHoverColor);
    }

    return renderedAny;
}

const FloraRepository::FloraCollection* FloraCollectionsPanelTab::GetSelectedCollection_(
    const std::vector<FloraRepository::FloraCollection>& collections) const {
    for (const auto& collection : collections) {
        if (BuildCollectionKey_(collection.type, collection.id) == selectedCollectionKey_) {
            return &collection;
        }
    }
    return nullptr;
}

uint64_t FloraCollectionsPanelTab::BuildCollectionKey_(const FloraRepository::CollectionType type, const uint32_t id) const {
    return (static_cast<uint64_t>(type) << 32) | id;
}
