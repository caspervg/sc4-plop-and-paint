#include "FloraPanelTab.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../SC4PlopAndPaintDirector.hpp"
#include "../common/BadgeUtils.hpp"
#include "../common/Constants.hpp"
#include "../common/Utils.hpp"
#include "../utils/Logger.h"

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
        ImGui::TextUnformatted("No flora loaded. Please ensure flora.cbor exists in the Plugins directory.");
        return;
    }

    RenderFilterUI_();
    ImGui::Separator();

    std::vector<size_t> filteredFloraIndices;
    BuildFilteredFloraIndices_(filteredFloraIndices);

    ImGui::Text("Showing %zu of %zu flora items", filteredFloraIndices.size(), flora_->GetFloraItems().size());
    if (director_->IsFloraPainting()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop painting")) {
            director_->StopFloraPainting();
        }
    }

    if (ImGui::BeginChild("FloraTableRegion", ImVec2(0, 0), false)) {
        RenderIndividualFloraTable_(filteredFloraIndices);
        if (filteredFloraIndices.empty()) {
            ImGui::TextDisabled("No flora items match the current filters.");
        }
    }
    ImGui::EndChild();

    thumbnailCache_.ProcessLoadQueue([this](const uint64_t key) {
        return LoadFloraTexture_(key);
    });
    RenderPaintModal_();
}

void FloraPanelTab::OnDeviceReset(const uint32_t deviceGeneration) {
    if (deviceGeneration != lastDeviceGeneration_) {
        thumbnailCache_.OnDeviceReset();
        lastDeviceGeneration_ = deviceGeneration;
    }
}

ImGuiTexture FloraPanelTab::LoadFloraTexture_(const uint64_t key) const {
    ImGuiTexture texture;
    if (!imguiService_) {
        return texture;
    }
    const auto data = flora_->GetFloraThumbnailStore().LoadThumbnail(key);
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
    ImGui::SameLine();
    if (ImGui::Button("Clear filters")) {
        searchBuf_[0] = '\0';
        favoritesOnly_ = false;
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

        if (!searchStr.empty()) {
            const bool nameMatch =
                ContainsCaseInsensitive(flora.visibleName, searchStr) ||
                ContainsCaseInsensitive(flora.exemplarName, searchStr) ||
                ContainsCaseInsensitive(FormatHexId(flora.instanceId.value()), searchStr) ||
                ContainsCaseInsensitive(FormatHexId(flora.groupId.value()), searchStr);
            if (!nameMatch) {
                continue;
            }
        }

        filteredIndices.push_back(i);
    }
}

void FloraPanelTab::RenderIndividualFloraTable_(const std::vector<size_t>& filteredIndices) {
    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBody |
        ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY;

    if (!ImGui::BeginTable("FloraTable", 4, tableFlags, ImVec2(0, 0))) {
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Thumb",
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
    ImGui::TableSetupScrollFreeze(0, 1);

    const auto& items = flora_->GetFloraItems();
    std::vector<size_t> sortedIndices(filteredIndices.begin(), filteredIndices.end());
    const auto compareStrings = [](const std::string& lhs, const std::string& rhs) {
        if (lhs < rhs) return -1;
        if (lhs > rhs) return 1;
        return 0;
    };
    const auto compareFloats = [](const float lhs, const float rhs) {
        if (lhs < rhs) return -1;
        if (lhs > rhs) return 1;
        return 0;
    };

    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsCount > 0) {
        std::ranges::sort(sortedIndices, [&](const size_t lhsIndex, const size_t rhsIndex) {
            const Flora& lhs = items[lhsIndex];
            const Flora& rhs = items[rhsIndex];

            for (int specIndex = 0; specIndex < specs->SpecsCount; ++specIndex) {
                const auto& spec = specs->Specs[specIndex];
                int cmp = 0;
                switch (spec.ColumnIndex) {
                case 1:
                    cmp = compareStrings(FloraDisplayName(lhs), FloraDisplayName(rhs));
                    break;
                case 2: {
                    const float lhsVolume = lhs.width * lhs.height * lhs.depth;
                    const float rhsVolume = rhs.width * rhs.height * rhs.depth;
                    cmp = compareFloats(lhsVolume, rhsVolume);
                    if (cmp == 0) cmp = compareFloats(lhs.width, rhs.width);
                    if (cmp == 0) cmp = compareFloats(lhs.height, rhs.height);
                    if (cmp == 0) cmp = compareFloats(lhs.depth, rhs.depth);
                    break;
                }
                default:
                    break;
                }

                if (cmp != 0) {
                    return spec.SortDirection == ImGuiSortDirection_Descending ? (cmp > 0) : (cmp < 0);
                }
            }

            return MakeGIKey(lhs.groupId.value(), lhs.instanceId.value()) <
                MakeGIKey(rhs.groupId.value(), rhs.instanceId.value());
        });
    }

    const float rowHeight = UI::iconRowHeight();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(sortedIndices.size()), rowHeight);
    while (clipper.Step()) {
        const int prefetchStart = std::max(0, clipper.DisplayStart - Cache::kPrefetchMargin);
        const int prefetchEnd = std::min(static_cast<int>(sortedIndices.size()), clipper.DisplayEnd + Cache::kPrefetchMargin);
        for (int i = prefetchStart; i < prefetchEnd; ++i) {
            const auto& flora = items[sortedIndices[i]];
            const uint64_t key = MakeGIKey(flora.groupId.value(), flora.instanceId.value());
            if (flora_->GetFloraThumbnailStore().HasThumbnail(key)) {
                thumbnailCache_.Request(key);
            }
        }

        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const size_t floraIndex = sortedIndices[i];
            const auto& f = items[floraIndex];
            const uint64_t key = MakeGIKey(f.groupId.value(), f.instanceId.value());
            const bool isFavorite = favorites_->IsFloraFavorite(f.groupId.value(), f.instanceId.value());

            ImGui::PushID(static_cast<int>(key));
            ImGui::TableNextRow(0, rowHeight);

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

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(FloraDisplayName(f).c_str());
            const bool renderedPills = RenderFloraPills_(f, true);
            if (!f.visibleName.empty() && !f.exemplarName.empty()) {
                if (renderedPills) {
                    ImGui::SameLine(0.0f, 4.0f);
                }
                ImGui::TextDisabled("%s", f.exemplarName.c_str());
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
    }
    if (flora.clusterNextType.has_value()) {
        renderPill("Multi-stage", kMultiStageColor, kMultiStageHoverColor);
    }

    return renderedAny;
}

void FloraPanelTab::QueuePaintForFlora_(const Flora& flora) {
    pendingPaint_.typeId = flora.instanceId.value();
    pendingPaint_.name = FloraDisplayName(flora);
    pendingPaint_.settings.activePalette.clear();
    pendingPaint_.open = true;
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
    ImGui::Separator();

    ImGui::TextUnformatted("Mode");
    int mode = static_cast<int>(pendingPaint_.settings.mode);
    ImGui::RadioButton("Direct paint",        &mode, static_cast<int>(PaintMode::Direct));
    ImGui::RadioButton("Paint along line",    &mode, static_cast<int>(PaintMode::Line));
    ImGui::RadioButton("Paint inside polygon",&mode, static_cast<int>(PaintMode::Polygon));
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
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Used only for direct paint mode. Line and polygon previews always use the outline overlay.");
    }

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
