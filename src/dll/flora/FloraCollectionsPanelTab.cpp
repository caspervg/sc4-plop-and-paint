#include "FloraCollectionsPanelTab.hpp"
#include "../paint/PolygonFillControls.hpp"

#include <algorithm>
#include <cstdio>

#include "../SC4PlopAndPaintDirector.hpp"
#include "../common/BadgeUtils.hpp"
#include "../common/Constants.hpp"
#include "../common/Utils.hpp"

using PanelUi::ButtonWidth;
using PanelUi::CheckboxWidth;
using PanelUi::Hint;
using PanelUi::kGroupGap;
using PanelUi::NextInRow;
using PanelUi::Tooltip;

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
            return "Multi-stage";
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
        ImGui::TextUnformatted("No flora collections yet.");
        Hint("Collections come from flora families and multi-stage flora. If the Flora tab is empty too, "
             "run the cache builder to make flora.cbor in the Plugins folder, then restart the game.");
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

    RenderResultLine_(filteredIndices.size(), collections.size());
    if (director_->IsFloraPainting()) {
        if (ImGui::SmallButton("Stop painting")) {
            director_->StopFloraPainting();
        }
        PanelUi::Tooltip(PanelUi::kStopPaintingHelp);
        RenderFloraStripperControls_(1);
    }
    else {
        RenderFloraStripperControls_();
    }
    ImGui::Separator();

    if (filteredIndices.empty()) {
        if (PanelUi::NoResults("No collections match these filters.")) {
            ClearFilters_();
        }
    }
    else {
        // The list takes about a third of the panel and the flora the rest. Both stay usable in
        // a short panel, which then scrolls.
        const float row = ImGui::GetFrameHeightWithSpacing();
        RenderCollectionsTable_(collections, filteredIndices, std::max(row * 6.5f, ImGui::GetContentRegionAvail().y * 0.35f));
        RenderSelectedCollectionPanel_(collections);
    }

    thumbnailCache_.ProcessLoadQueue([this](const uint64_t key) {
        return LoadFloraTexture_(key);
    });
    RenderPaintModal_();
}

void FloraCollectionsPanelTab::RenderResultLine_(const size_t shown, const size_t total) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Showing %zu of %zu collections", shown, total);
    const int active = static_cast<int>(nameFilterBuf_[0] != '\0') + static_cast<int>(iidFilterBuf_[0] != '\0') +
                       static_cast<int>(typeFilter_ != 0) + static_cast<int>(favoritesOnly_);
    if (active > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("%d filter%s on", active, active == 1 ? "" : "s");
    }
    ImGui::SameLine(0.0f, kGroupGap);
    ImGui::BeginDisabled(active == 0);
    if (ImGui::SmallButton("Clear filters")) {
        ClearFilters_();
    }
    ImGui::EndDisabled();
    Tooltip(active == 0 ? "No filters are on." : "Show all collections again.");
}

void FloraCollectionsPanelTab::ClearFilters_() {
    nameFilterBuf_[0] = '\0';
    iidFilterBuf_[0] = '\0';
    typeFilter_ = 0;
    favoritesOnly_ = false;
}

void FloraCollectionsPanelTab::OnDeviceReset(const uint32_t deviceGeneration) {
    if (deviceGeneration != lastDeviceGeneration_) {
        thumbnailCache_.OnDeviceReset();
        previewCache_.OnDeviceReset();
        lastDeviceGeneration_ = deviceGeneration;
    }
}

ImGuiTexture FloraCollectionsPanelTab::LoadFloraTexture_(const uint64_t key) const {
    ImGuiTexture texture;
    if (!imguiService_) {
        return texture;
    }

    auto data = flora_->GetFloraThumbnailStore().LoadThumbnail(key, static_cast<uint32_t>(UI::kIconSize));
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

    NextInRow(1, UI::dropdownWidth());
    static constexpr const char* kTypeOptions[] = {"All types", "Families", "Multi-stage"};
    ImGui::SetNextItemWidth(UI::dropdownWidth());
    ImGui::Combo("##FloraCollectionType", &typeFilter_, kTypeOptions, IM_ARRAYSIZE(kTypeOptions));

    NextInRow(2, CheckboxWidth("Favorites only"));
    ImGui::Checkbox("Favorites only", &favoritesOnly_);
    Tooltip("Show the collections that you starred, and those with flora that you starred.");
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
            const bool starred = favorites_->IsFamilyFavorite(
                FavoritesRepository::FloraCollectionFavoriteKey(static_cast<uint32_t>(collection.type), collection.id));
            const bool hasFavoriteMember = std::ranges::any_of(collection.members, [&favoriteFloraIds](const Flora* flora) {
                return flora &&
                    favoriteFloraIds.contains(MakeGIKey(flora->groupId.value(), flora->instanceId.value()));
            });
            if (!starred && !hasFavoriteMember) {
                continue;
            }
        }

        filteredIndices.push_back(i);
    }
}

void FloraCollectionsPanelTab::RenderCollectionsTable_(const std::vector<FloraRepository::FloraCollection>& collections,
                                                       const std::vector<size_t>& filteredIndices, const float height) {
    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBody |
        ImGuiTableFlags_ScrollY;

    if (!ImGui::BeginTable("FloraCollectionsTable", 5, tableFlags, ImVec2(0, height))) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, UI::typeColumnWidth());
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Instance ID", ImGuiTableColumnFlags_WidthFixed, UI::instanceIdColumnWidth());
    ImGui::TableSetupColumn("Items", ImGuiTableColumnFlags_WidthFixed, UI::propsColumnWidth());
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, UI::familyActionColWidth());
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(filteredIndices.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const size_t collectionIndex = filteredIndices[static_cast<size_t>(row)];
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
            ImGui::BeginDisabled(collection.members.empty());
            if (ImGui::SmallButton("Paint")) {
                selectedCollectionKey_ = BuildCollectionKey_(collection.type, collection.id);
                QueuePaintForCollection_(collection);
            }
            ImGui::EndDisabled();
            if (collection.members.empty()) {
                Tooltip("This collection has no flora.");
            }
            ImGui::SameLine();
            const uint64_t favoriteKey =
                FavoritesRepository::FloraCollectionFavoriteKey(static_cast<uint32_t>(collection.type), collection.id);
            const bool favorite = favorites_->IsFamilyFavorite(favoriteKey);
            if (ImGui::SmallButton(favorite ? "Unstar" : "Star")) {
                favorites_->ToggleFamilyFavorite(favoriteKey);
            }
            Tooltip(favorite ? "Remove from favorites" : "Add to favorites");

            ImGui::PopID();
        }
    }

    ImGui::EndTable();
}

void FloraCollectionsPanelTab::RenderSelectedCollectionPanel_(const std::vector<FloraRepository::FloraCollection>& collections) {
    const FloraRepository::FloraCollection* collection = GetSelectedCollection_(collections);
    if (!collection) {
        return;
    }

    ImGui::TextUnformatted(collection->name.c_str());

    // What can be done with it comes before its flora, so that it does not scroll out of view.
    const bool empty = collection->members.empty();
    const bool family = collection->type == FloraRepository::CollectionType::Family;
    ImGui::BeginDisabled(empty);
    if (ImGui::Button("Paint collection")) {
        QueuePaintForCollection_(*collection);
    }
    ImGui::EndDisabled();
    Tooltip(empty ? "This collection has no flora."
                  : family ? "Painting picks at random from the flora of this family."
                           : "Painting picks at random from the stages of this flora.");
    ImGui::SameLine(0.0f, kGroupGap);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%zu flora items", collection->members.size());
    Tooltip("Collections are made from your plugins and cannot be changed.");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", FormatHexId(collection->id).c_str());

    if (empty) {
        Hint("This collection has no flora.");
        return;
    }
    RenderMemberTable_(*collection,
                       std::max(UI::iconRowHeight() * 3.0f + ImGui::GetTextLineHeightWithSpacing(), ImGui::GetContentRegionAvail().y));
}

void FloraCollectionsPanelTab::RenderMemberTable_(const FloraRepository::FloraCollection& collection, const float height) {
    if (!ImGui::BeginTable("FloraCollectionEntries", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                           ImVec2(0, height))) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("##icon", ImGuiTableColumnFlags_WidthFixed, UI::iconColumnWidth());
    ImGui::TableSetupColumn("Flora", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Instance ID", ImGuiTableColumnFlags_WidthFixed, UI::instanceIdColumnWidth());
    ImGui::TableSetupColumn("##star", ImGuiTableColumnFlags_WidthFixed, UI::favoriteColumnWidth());
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(collection.members.size()), UI::iconRowHeight());
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const Flora* flora = collection.members[static_cast<size_t>(row)];
            ImGui::PushID(row);
            ImGui::TableNextRow(0, UI::iconRowHeight());

            ImGui::TableNextColumn();
            if (flora) {
                const uint64_t key = MakeGIKey(flora->groupId.value(), flora->instanceId.value());
                if (flora_->GetFloraThumbnailStore().HasThumbnail(key)) {
                    thumbnailCache_.Request(key);
                }
                RenderThumbnailWithPreview_(thumbnailCache_.Get(key), flora_->GetFloraThumbnailStore(), key);
            }
            else {
                RenderThumbnail_(std::nullopt);
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
                RenderFavoriteButton_(*flora);
            }
            else {
                ImGui::TextDisabled("-");
            }

            ImGui::PopID();
        }
    }

    ImGui::EndTable();
}

void FloraCollectionsPanelTab::RenderPaintModal_() {
    if (pendingPaint_.open) {
        director_->ApplyGridChoice(&PaintGridChoices::flora, pendingPaint_.settings);
        ImGui::OpenPopup("Paint flora collection");
        pendingPaint_.open = false;
    }

    PanelUi::LimitDialogHeight();
    if (!ImGui::BeginPopupModal("Paint flora collection", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::Text("Collection: %s", pendingPaint_.name.c_str());
    ImGui::TextDisabled("%s", pendingPaint_.detail.c_str());
    ImGui::Separator();

    RenderPaintOptions_(pendingPaint_.settings, "flora", false);

    ImGui::Checkbox("Place on roads and lots", &pendingPaint_.settings.placeFloraOverObjects);
    PanelUi::Tooltip("Flora can stand on roads, buildings, props and other flora. Flora does not go into water that it cannot grow in.");

    ImGui::Separator();
    if (RenderStartCancel_()) {
        ReleaseImGuiInputCapture_();
        director_->SaveGridChoice(&PaintGridChoices::flora, pendingPaint_.settings);
        const RecentPaintSource source{
            .sourceKind = pendingPaint_.sourceKind,
            .sourceId = pendingPaint_.sourceId
        };
        director_->StartFloraPainting(pendingPaint_.typeId, pendingPaint_.settings, pendingPaint_.name, source);
    }

    ImGui::EndPopup();
}

void FloraCollectionsPanelTab::QueuePaintForCollection_(const FloraRepository::FloraCollection& collection) {
    pendingPaint_.typeId = collection.palette.empty() ? 0 : collection.palette.front().propID.value();
    pendingPaint_.sourceKind = collection.type == FloraRepository::CollectionType::Family
        ? RecentPaintEntry::SourceKind::FloraFamily
        : RecentPaintEntry::SourceKind::FloraChain;
    pendingPaint_.sourceId = collection.id;
    pendingPaint_.name = collection.name;
    pendingPaint_.detail = collection.type == FloraRepository::CollectionType::Family
        ? "Picks at random from the flora of this family."
        : "Picks at random from the stages of this flora.";
    pendingPaint_.settings.activePalette = collection.palette;

    // Already painting: keeps the current mode and rotation, without the dialog.
    if (director_->IsFloraPainting() && pendingPaint_.typeId != 0) {
        ReleaseImGuiInputCapture_();
        const RecentPaintSource source{.sourceKind = pendingPaint_.sourceKind, .sourceId = pendingPaint_.sourceId};
        if (director_->SwitchFloraPaintingTarget(pendingPaint_.typeId, collection.name, collection.palette, source)) {
            return;
        }
    }
    pendingPaint_.open = true;
}

void FloraCollectionsPanelTab::RenderFavoriteButton_(const Flora& flora) const {
    const bool isFavorite = favorites_->IsFloraFavorite(flora.groupId.value(), flora.instanceId.value());
    if (ImGui::SmallButton(isFavorite ? "Unstar" : "Star")) {
        favorites_->ToggleFloraFavorite(flora.groupId.value(), flora.instanceId.value());
    }
    Tooltip(isFavorite ? "Remove this flora from favorites" : "Add this flora to favorites");
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
        Tooltip("This flora is in a flora family.");
    }
    if (flora.clusterNextType.has_value()) {
        renderPill("Multi-stage", kMultiStageColor, kMultiStageHoverColor);
        Tooltip("This flora grows into other stages.");
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
