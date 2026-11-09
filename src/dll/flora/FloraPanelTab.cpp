#include "FloraPanelTab.hpp"
#include "../paint/PolygonFillControls.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <tuple>

#include "../SC4PlopAndPaintDirector.hpp"
#include "../common/BadgeUtils.hpp"
#include "../common/Constants.hpp"
#include "../common/Utils.hpp"
#include "../utils/Logger.h"

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
}

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
        ImGui::TextWrapped("No flora loaded. Run the cache builder to make flora.cbor in the Plugins folder, "
                           "then restart the game.");
        return;
    }

    IndexPacks_();
    RenderFilterUI_();
    ImGui::Separator();

    RefreshShown_();
    RenderResultLine_();
    RenderTools_();

    if (shown_.empty()) {
        if (PanelUi::NoResults("No flora match these filters.")) {
            ClearFilters_();
        }
    }
    else if (gridView_) {
        if (ImGui::BeginChild("FloraGridRegion", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
            RenderGrid_();
        }
        ImGui::EndChild();
    }
    else {
        if (ImGui::BeginChild("FloraTableRegion", ImVec2(0, 0), false)) {
            RenderIndividualFloraTable_();
        }
        ImGui::EndChild();
    }

    thumbnailCache_.ProcessLoadQueue([this](const uint64_t key) {
        return LoadFloraTexture_(key);
    });
    RenderPaintModal_();
}

void FloraPanelTab::RenderResultLine_() {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Showing %zu of %zu flora items", shown_.size(), flora_->GetFloraItems().size());
    const int active = static_cast<int>(searchBuf_[0] != '\0') + static_cast<int>(favoritesOnly_) +
                       static_cast<int>(packFilter_.IsActive());
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
    Tooltip(active == 0 ? "No filters are on." : "Show all flora again.");
    if (RenderViewControls_()) {
        thumbnailCache_.Clear();
    }
}

void FloraPanelTab::ClearFilters_() {
    searchBuf_[0] = '\0';
    favoritesOnly_ = false;
    packFilter_.Clear();
}

void FloraPanelTab::RenderTools_() {
    size_t index = 0;
    if (director_->IsFloraPainting()) {
        if (ImGui::SmallButton("Stop painting")) {
            director_->StopFloraPainting();
        }
        PanelUi::Tooltip(PanelUi::kStopPaintingHelp);
        ++index;
    }
    if (director_->IsFloraPicking()) {
        NextInRow(index, ButtonWidth("Stop picking"));
        if (ImGui::SmallButton("Stop picking##FloraPicker")) {
            director_->StopFloraPicking();
        }
    }
    else {
        NextInRow(index, ButtonWidth("Pick flora"));
        if (ImGui::SmallButton("Pick flora")) {
            ReleaseImGuiInputCapture_();
            director_->StartFloraPicking([this](const PickedFlora& picked) {
                HandlePickedFlora_(picked);
            });
        }
        Tooltip("Click flora in the city to select it here.");
    }
    RenderFloraStripperControls_(1);
}

void FloraPanelTab::OnDeviceReset(const uint32_t deviceGeneration) {
    if (deviceGeneration != lastDeviceGeneration_) {
        thumbnailCache_.OnDeviceReset();
        previewCache_.OnDeviceReset();
        lastDeviceGeneration_ = deviceGeneration;
    }
}

ImGuiTexture FloraPanelTab::LoadFloraTexture_(const uint64_t key) const {
    ImGuiTexture texture;
    if (!imguiService_) {
        return texture;
    }
    const auto data = flora_->GetFloraThumbnailStore().LoadThumbnail(key, static_cast<uint32_t>(ShownThumbnailSize_()));
    if (!data.has_value()) {
        return texture;
    }
    texture.Create(imguiService_, data->width, data->height, data->rgba.data());
    return texture;
}

void FloraPanelTab::IndexPacks_() {
    const auto& items = flora_->GetFloraItems();
    if (items.size() == indexedFloraCount_) {
        return;
    }
    indexedFloraCount_ = items.size();
    const PackCatalogRepository* catalog = director_->GetPackCatalog();
    packFilter_.Begin(catalog);
    floraPacks_.clear();
    floraPacks_.reserve(items.size());
    for (const auto& flora : items) {
        const auto rows = catalog ? catalog->ExemplarRows(flora.groupId.value(), flora.instanceId.value())
                                  : std::span<const PackCatalogRepository::ExemplarRow>{};
        packFilter_.Count(rows);
        floraPacks_.push_back(rows);
    }
}

void FloraPanelTab::RenderFilterUI_() {
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##SearchFlora", "Search flora...", searchBuf_, sizeof(searchBuf_))) {
        // searchBuf_ is updated in-place
    }
    packFilter_.Render("##FloraPack", "flora");
    ImGui::Checkbox("Favorites only", &favoritesOnly_);
    Tooltip("Show only the flora that you starred.");
}

void FloraPanelTab::RefreshShown_() {
    const auto& items = flora_->GetFloraItems();
    const size_t favoriteCount = favoritesOnly_ ? favorites_->GetFavoriteFloraIds().size() : 0;
    if (appliedSearch_ != searchBuf_ || appliedFavoritesOnly_ != favoritesOnly_ || appliedFavoriteCount_ != favoriteCount ||
        appliedPack_ != packFilter_.Selected() || appliedItemCount_ != items.size()) {
        appliedSearch_ = searchBuf_;
        appliedFavoritesOnly_ = favoritesOnly_;
        appliedFavoriteCount_ = favoriteCount;
        appliedPack_ = packFilter_.Selected();
        appliedItemCount_ = items.size();
        BuildFilteredFloraIndices_(shown_);
        sortDirty_ = true;
    }
    if (appliedGridView_ != gridView_) {
        appliedGridView_ = gridView_;
        sortDirty_ = true;
    }
    if (sortDirty_) {
        SortShown_();
    }
}

void FloraPanelTab::SortShown_() {
    sortDirty_ = false;
    const auto& items = flora_->GetFloraItems();
    const bool bySize = !gridView_ && sortColumn_ == 2;
    const bool descending = !gridView_ && sortDescending_;
    std::ranges::sort(shown_, [&](const size_t lhsIndex, const size_t rhsIndex) {
        const Flora& lhs = items[lhsIndex];
        const Flora& rhs = items[rhsIndex];
        if (bySize) {
            const auto size = [](const Flora& f) { return std::tuple(f.width * f.height * f.depth, f.width, f.height, f.depth); };
            if (size(lhs) != size(rhs)) {
                return descending ? size(rhs) < size(lhs) : size(lhs) < size(rhs);
            }
        }
        else {
            const std::string lhsName = FloraDisplayName(lhs);
            const std::string rhsName = FloraDisplayName(rhs);
            if (lhsName != rhsName) {
                return descending ? rhsName < lhsName : lhsName < rhsName;
            }
        }
        return MakeGIKey(lhs.groupId.value(), lhs.instanceId.value()) < MakeGIKey(rhs.groupId.value(), rhs.instanceId.value());
    });
}

void FloraPanelTab::RenderFloraMenu_(const Flora& flora) {
    selectedFloraKey_ = MakeGIKey(flora.groupId.value(), flora.instanceId.value());
    if (ImGui::Selectable("Paint")) {
        QueuePaintForFlora_(flora);
    }
    const bool isFavorite = favorites_->IsFloraFavorite(flora.groupId.value(), flora.instanceId.value());
    if (ImGui::Selectable(isFavorite ? "Remove from favorites" : "Add to favorites")) {
        favorites_->ToggleFloraFavorite(flora.groupId.value(), flora.instanceId.value());
    }
}

void FloraPanelTab::HandleItemClick_(const Flora& flora, const uint64_t key, const bool doubleClick) {
    selectedFloraKey_ = key;
    if (doubleClick || director_->IsFloraPainting()) {
        QueuePaintForFlora_(flora);
    }
}

void FloraPanelTab::BuildFilteredFloraIndices_(std::vector<size_t>& filteredIndices) const {
    const auto& items = flora_->GetFloraItems();
    const std::string_view searchStr = searchBuf_;
    const auto& favoriteFloraIds = favorites_->GetFavoriteFloraIds();

    filteredIndices.clear();
    filteredIndices.reserve(items.size());
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& flora = items[i];
        if (favoritesOnly_ &&
            !favoriteFloraIds.contains(MakeGIKey(flora.groupId.value(), flora.instanceId.value()))) {
            continue;
        }
        const auto packs = i < floraPacks_.size() ? floraPacks_[i] : std::span<const PackCatalogRepository::ExemplarRow>{};
        if (!packFilter_.Passes(packs)) {
            continue;
        }

        if (!searchStr.empty()) {
            const bool nameMatch =
                ContainsCaseInsensitive(flora.visibleName, searchStr) ||
                ContainsCaseInsensitive(flora.exemplarName, searchStr) ||
                ContainsCaseInsensitive(FormatHexId(flora.instanceId.value()), searchStr) ||
                ContainsCaseInsensitive(FormatHexId(flora.groupId.value()), searchStr) ||
                packFilter_.PackNameContains(packs, searchStr);
            if (!nameMatch) {
                continue;
            }
        }

        filteredIndices.push_back(i);
    }
}

void FloraPanelTab::RenderIndividualFloraTable_() {
    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBody |
        ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY;

    if (!ImGui::BeginTable("FloraTable", 4, tableFlags, ImVec2(0, 0))) {
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("##Thumb",
                            ImGuiTableColumnFlags_WidthFixed |
                            ImGuiTableColumnFlags_NoSort,
                            UI::iconColumnWidth());
    ImGui::TableSetupColumn("Name",
                            ImGuiTableColumnFlags_NoHide |
                            ImGuiTableColumnFlags_DefaultSort |
                            ImGuiTableColumnFlags_PreferSortAscending);
    ImGui::TableSetupColumn("Size (m)",
                            ImGuiTableColumnFlags_WidthFixed |
                            ImGuiTableColumnFlags_PreferSortAscending,
                            UI::propSizeColumnWidth());
    ImGui::TableSetupColumn("Action",
                            ImGuiTableColumnFlags_WidthFixed |
                            ImGuiTableColumnFlags_NoSort,
                            UI::actionColumnWidth());
    ImGui::TableHeadersRow();

    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty) {
        if (specs->SpecsCount > 0) {
            sortColumn_ = specs->Specs[0].ColumnIndex;
            sortDescending_ = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
            SortShown_();
        }
        specs->SpecsDirty = false;
    }

    const auto& items = flora_->GetFloraItems();
    const float rowHeight = UI::iconRowHeight();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(shown_.size()), rowHeight);
    while (clipper.Step()) {
        const int prefetchStart = std::max(0, clipper.DisplayStart - Cache::kPrefetchMargin);
        const int prefetchEnd = std::min(static_cast<int>(shown_.size()), clipper.DisplayEnd + Cache::kPrefetchMargin);
        for (int i = prefetchStart; i < prefetchEnd; ++i) {
            const auto& flora = items[shown_[i]];
            const uint64_t key = MakeGIKey(flora.groupId.value(), flora.instanceId.value());
            if (flora_->GetFloraThumbnailStore().HasThumbnail(key)) {
                thumbnailCache_.Request(key);
            }
        }

        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& f = items[shown_[i]];
            const uint64_t key = MakeGIKey(f.groupId.value(), f.instanceId.value());
            const bool isFavorite = favorites_->IsFloraFavorite(f.groupId.value(), f.instanceId.value());

            ImGui::PushID(static_cast<int>(key));
            ImGui::TableNextRow(0, rowHeight);

            ImGui::TableNextColumn();
            if (ImGui::Selectable("##row", key == selectedFloraKey_,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap |
                                  ImGuiSelectableFlags_AllowDoubleClick,
                                  ImVec2(0, rowHeight))) {
                HandleItemClick_(f, key, ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left));
            }
            if (ImGui::BeginPopupContextItem("##FloraMenu")) {
                RenderFloraMenu_(f);
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            auto thumbId = thumbnailCache_.Get(key);
            RenderThumbnailWithPreview_(thumbId, flora_->GetFloraThumbnailStore(), key);

            // Two lines, so that every row has the same height. The pack is in the tooltip. The
            // row's selectable spans this cell, so the tooltip tests the rectangle.
            ImGui::TableNextColumn();
            ImGui::BeginGroup();
            ImGui::TextUnformatted(FloraDisplayName(f).c_str());
            const bool renderedPills = RenderFloraPills_(f, true);
            if (!f.visibleName.empty() && !f.exemplarName.empty()) {
                if (renderedPills) {
                    ImGui::SameLine(0.0f, 4.0f);
                }
                ImGui::TextDisabled("%s", f.exemplarName.c_str());
            }
            ImGui::EndGroup();
            const PackCatalogRepository* catalog = director_->GetPackCatalog();
            const auto packs = catalog ? catalog->ExemplarRows(f.groupId.value(), f.instanceId.value())
                                       : std::span<const PackCatalogRepository::ExemplarRow>{};
            if (const TexturePack* pack = packs.empty() ? nullptr : catalog->Pack(static_cast<int32_t>(packs.front()[3]));
                pack && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax())) {
                ImGui::SetTooltip("From %s%s", pack->name.c_str(), packs.size() > 1 ? " and more" : "");
            }

            ImGui::TableNextColumn();
            if (f.width >= 0.0f && f.height >= 0.0f && f.depth >= 0.0f) {
                ImGui::Text("%.1f x %.1f x %.1f", f.width, f.height, f.depth);
            } else {
                ImGui::TextDisabled("N/A");
            }

            ImGui::TableNextColumn();
            if (ImGui::Button("Paint")) {
                QueuePaintForFlora_(f);
            }
            ImGui::SameLine();
            if (ImGui::Button(isFavorite ? "Unstar" : "Star")) {
                favorites_->ToggleFloraFavorite(f.groupId.value(), f.instanceId.value());
            }
            Tooltip(isFavorite ? "Remove from favorites" : "Add to favorites");

            ImGui::PopID();
        }
    }

    ImGui::EndTable();
}

bool FloraPanelTab::RenderFloraPills_(const Flora& flora, const bool startOnNewLine) const {
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
        Tooltip("This flora is in a flora family. See the Flora Collections tab.");
    }
    if (flora.clusterNextType.has_value()) {
        renderPill("Multi-stage", kMultiStageColor, kMultiStageHoverColor);
        Tooltip("This flora grows into other stages. See the Flora Collections tab.");
    }

    return renderedAny;
}

void FloraPanelTab::RenderGrid_() {
    const auto& items = flora_->GetFloraItems();
    const std::vector<size_t>& sortedIndices = shown_;

    ThumbnailStore& store = flora_->GetFloraThumbnailStore();
    const size_t visible = ThumbnailGrid::Render(sortedIndices.size(), gridTileSize_,
        [&](const size_t index) {
            const Flora& f = items[sortedIndices[index]];
            return MakeGIKey(f.groupId.value(), f.instanceId.value());
        },
        [&](const size_t index) {
        const Flora& f = items[sortedIndices[index]];
        const uint64_t key = MakeGIKey(f.groupId.value(), f.instanceId.value());
        const bool hasThumbnail = store.HasThumbnail(key);
        const bool isFavorite = favorites_->IsFloraFavorite(f.groupId.value(), f.instanceId.value());

        const auto texture = thumbnailCache_.Get(key);
        if (!texture && hasThumbnail) {
            thumbnailCache_.Request(key);
        }
        RenderGridTile_(texture);
        if (!hasThumbnail) {
            ThumbnailGrid::LabelLastTile(FloraDisplayName(f).c_str());
        }
        ThumbnailGrid::DecorateLastTile(key == selectedFloraKey_, isFavorite);

        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(FloraDisplayName(f).c_str());
            if (!f.visibleName.empty() && !f.exemplarName.empty()) {
                ImGui::TextDisabled("%s", f.exemplarName.c_str());
            }
            if (f.width >= 0.0f && f.height >= 0.0f && f.depth >= 0.0f) {
                ImGui::Text("%.1f x %.1f x %.1f m", f.width, f.height, f.depth);
            }
            const PackCatalogRepository* catalog = director_->GetPackCatalog();
            const auto packs = catalog ? catalog->ExemplarRows(f.groupId.value(), f.instanceId.value())
                                       : std::span<const PackCatalogRepository::ExemplarRow>{};
            if (const TexturePack* pack = packs.empty() ? nullptr : catalog->Pack(static_cast<int32_t>(packs.front()[3]))) {
                ImGui::TextDisabled("%s%s", pack->name.c_str(), packs.size() > 1 ? " and more" : "");
            }
            RenderLargePreview_(store, key);
            ImGui::EndTooltip();
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            HandleItemClick_(f, key, ImGui::GetMouseClickedCount(ImGuiMouseButton_Left) >= 2);
        }

        if (ImGui::BeginPopupContextItem("##FloraTileMenu")) {
            RenderFloraMenu_(f);
            ImGui::EndPopup();
        }
    });
    thumbnailCache_.FitVisible(visible + Cache::kMaxLoadPerFrame);
}

void FloraPanelTab::QueuePaintForFlora_(const Flora& flora) {
    selectedFloraKey_ = MakeGIKey(flora.groupId.value(), flora.instanceId.value());
    if (director_->IsFloraPainting()) {
        // Keeps the current paint mode and rotation: no dialog.
        ReleaseImGuiInputCapture_();
        if (director_->SwitchFloraPaintingTarget(flora.instanceId.value(), FloraDisplayName(flora))) {
            return;
        }
    }
    pendingPaint_.typeId = flora.instanceId.value();
    pendingPaint_.name = FloraDisplayName(flora);
    pendingPaint_.settings.activePalette.clear();
    pendingPaint_.open = true;
}

void FloraPanelTab::HandlePickedFlora_(const PickedFlora& picked) {
    if (!flora_) {
        return;
    }

    const Flora* flora = flora_->FindFloraByInstanceId(picked.floraType);
    if (!flora) {
        LOG_INFO("Picked flora 0x{:08X}, but it is not present in the flora cache; using ID-only paint target",
                 picked.floraType);
        favoritesOnly_ = false;

        pendingPaint_.typeId = picked.floraType;
        pendingPaint_.name = FormatHexId(picked.floraType);
        pendingPaint_.settings.activePalette.clear();
        pendingPaint_.settings.mode = PaintMode::Direct;
        pendingPaint_.settings.rotation = picked.orientation & 3;
        pendingPaint_.open = true;
        return;
    }

    std::snprintf(searchBuf_, sizeof(searchBuf_), "%s", FloraDisplayName(*flora).c_str());
    favoritesOnly_ = false;
    packFilter_.Clear();
    selectedFloraKey_ = MakeGIKey(flora->groupId.value(), flora->instanceId.value());

    pendingPaint_.typeId = flora->instanceId.value();
    pendingPaint_.name = FloraDisplayName(*flora);
    pendingPaint_.settings.activePalette.clear();
    pendingPaint_.settings.mode = PaintMode::Direct;
    pendingPaint_.settings.rotation = picked.orientation & 3;
    pendingPaint_.open = true;
}

void FloraPanelTab::RenderPaintModal_() {
    if (pendingPaint_.open) {
        director_->ApplyGridChoice(&PaintGridChoices::flora, pendingPaint_.settings);
        ImGui::OpenPopup("Paint flora");
        pendingPaint_.open = false;
    }

    PanelUi::LimitDialogHeight();
    if (!ImGui::BeginPopupModal("Paint flora", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::Text("Flora: %s", pendingPaint_.name.c_str());
    ImGui::Separator();

    RenderPaintOptions_(pendingPaint_.settings, "flora", false);

    ImGui::Checkbox("Place on roads and lots", &pendingPaint_.settings.placeFloraOverObjects);
    PanelUi::Tooltip("Flora can stand on roads, buildings, props and other flora. Flora does not go into water that it cannot grow in.");

    ImGui::Separator();
    if (RenderStartCancel_()) {
        ReleaseImGuiInputCapture_();
        director_->SaveGridChoice(&PaintGridChoices::flora, pendingPaint_.settings);
        director_->StartFloraPainting(pendingPaint_.typeId, pendingPaint_.settings, pendingPaint_.name);
    }

    ImGui::EndPopup();
}
