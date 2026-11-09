#include "FamiliesPanelTab.hpp"
#include "../paint/PolygonFillControls.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <optional>

#include "../common/BadgeUtils.hpp"
#include "../common/Utils.hpp"

using PanelUi::ButtonWidth;
using PanelUi::CheckboxWidth;
using PanelUi::Hint;
using PanelUi::kGroupGap;
using PanelUi::NextInRow;
using PanelUi::Tooltip;

namespace {
    std::string FormatFamilyId(const std::optional<uint32_t> familyId) {
        if (!familyId.has_value()) {
            return "-";
        }

        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "0x%08X", familyId.value());
        return buffer;
    }

    bool MatchesIidFilter(const std::optional<uint32_t> familyId, const std::string& filter) {
        if (filter.empty()) {
            return true;
        }
        if (!familyId.has_value()) {
            return false;
        }

        const std::string formatted = FormatFamilyId(familyId);
        std::string compact = formatted;
        compact.erase(std::remove(compact.begin(), compact.end(), 'x'), compact.end());
        compact.erase(std::remove(compact.begin(), compact.end(), 'X'), compact.end());

        const std::string normalizedFilter = ToUpperCopy(filter);
        return ToUpperCopy(formatted).contains(normalizedFilter) || ToUpperCopy(compact).contains(normalizedFilter);
    }
}

const char* FamiliesPanelTab::GetTabName() const {
    return "Prop Families";
}

void FamiliesPanelTab::OnRender() {
    if (!imguiService_) {
        Hint("The ImGui service is not available.");
        return;
    }

    const size_t totalCount = props_->GetAutoFamilies().size() + favorites_->GetUserFamilies().size();
    if (totalCount == 0) {
        ImGui::TextUnformatted("No families yet.");
        Hint("Built-in families show after the prop data loads. Make your own family here, then add props from the Props tab.");
        if (ImGui::Button("New family")) {
            OpenNewFamily_();
        }
        RenderNewFamilyPopup_();
        return;
    }

    if (selectedFamilyIndex_ >= totalCount) {
        selectedFamilyIndex_ = totalCount - 1;
    }

    RenderFilters_();
    ImGui::Separator();

    const std::vector<FamilyRow> rows = BuildRows_();
    if (!rows.empty() && std::ranges::none_of(rows, [this](const FamilyRow& row) {
            return row.combinedIndex == selectedFamilyIndex_;
        })) {
        selectedFamilyIndex_ = rows.front().combinedIndex;
    }

    RenderResultLine_(rows.size(), totalCount);
    RenderTools_();
    ImGui::Separator();

    if (rows.empty()) {
        if (PanelUi::NoResults("No families match these filters.")) {
            ClearFilters_();
        }
    }
    else {
        // The list takes about a third of the panel and the props the rest. Both stay usable in
        // a short panel, which then scrolls.
        const float row = ImGui::GetFrameHeightWithSpacing();
        RenderFamilyTable_(rows, std::max(row * 6.5f, ImGui::GetContentRegionAvail().y * 0.35f));
        RenderDetails_();
    }

    RenderNewFamilyPopup_();
    RenderPaintOptionsPopup_();
}

void FamiliesPanelTab::RenderFilters_() {
    static char nameFilterBuf[256] = {};
    if (nameFilter_ != nameFilterBuf) {
        std::strncpy(nameFilterBuf, nameFilter_.c_str(), sizeof(nameFilterBuf) - 1);
        nameFilterBuf[sizeof(nameFilterBuf) - 1] = '\0';
    }

    static char iidFilterBuf[64] = {};
    if (iidFilter_ != iidFilterBuf) {
        std::strncpy(iidFilterBuf, iidFilter_.c_str(), sizeof(iidFilterBuf) - 1);
        iidFilterBuf[sizeof(iidFilterBuf) - 1] = '\0';
    }

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##SearchFamiliesByName", "Search family name...", nameFilterBuf, sizeof(nameFilterBuf))) {
        nameFilter_ = nameFilterBuf;
    }

    ImGui::SetNextItemWidth(UI::iidFilterWidth());
    if (ImGui::InputTextWithHint("##SearchFamiliesByIid", "Filter IID (0x...)", iidFilterBuf, sizeof(iidFilterBuf))) {
        iidFilter_ = iidFilterBuf;
    }
    NextInRow(1, CheckboxWidth("Favorites only"));
    ImGui::Checkbox("Favorites only", &favoritesOnly_);
    Tooltip("Show only the families that you starred.");
}

std::vector<FamiliesPanelTab::FamilyRow> FamiliesPanelTab::BuildRows_() const {
    const auto& autoFamilies = props_->GetAutoFamilies();
    const auto& autoFamilyIds = props_->GetAutoFamilyIds();
    const auto& userFamilies = favorites_->GetUserFamilies();
    const size_t autoCount = autoFamilies.size();

    std::vector<FamilyRow> rows;
    rows.reserve(autoCount + userFamilies.size());
    for (size_t i = 0; i < autoCount + userFamilies.size(); ++i) {
        const bool isAuto = i < autoCount;
        const PropFamily* family = isAuto ? &autoFamilies[i] : &userFamilies[i - autoCount];
        const std::optional<uint32_t> familyId = isAuto && i < autoFamilyIds.size()
            ? std::optional<uint32_t>(autoFamilyIds[i])
            : std::nullopt;
        const uint64_t favoriteKey = FavoritesRepository::FamilyFavoriteKey(
            isAuto, isAuto ? familyId.value_or(0) : (family->persistentId ? family->persistentId->value() : 0));

        if (favoritesOnly_ && !favorites_->IsFamilyFavorite(favoriteKey)) {
            continue;
        }
        if (!ContainsCaseInsensitive(family->name, nameFilter_) || !MatchesIidFilter(familyId, iidFilter_)) {
            continue;
        }
        rows.push_back({.combinedIndex = i, .isAuto = isAuto, .family = family, .familyId = familyId, .favoriteKey = favoriteKey});
    }
    return rows;
}

void FamiliesPanelTab::RenderResultLine_(const size_t shown, const size_t total) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Showing %zu of %zu families", shown, total);
    const int active = static_cast<int>(!nameFilter_.empty()) + static_cast<int>(!iidFilter_.empty()) +
                       static_cast<int>(favoritesOnly_);
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
    Tooltip(active == 0 ? "No filters are on." : "Show all families again.");
}

void FamiliesPanelTab::ClearFilters_() {
    nameFilter_.clear();
    iidFilter_.clear();
    favoritesOnly_ = false;
}

void FamiliesPanelTab::RenderTools_() {
    ImGui::AlignTextToFramePadding();
    if (ImGui::SmallButton("New family")) {
        OpenNewFamily_();
    }
    if (director_->IsPropPainting()) {
        NextInRow(1, ButtonWidth("Stop painting"));
        if (ImGui::SmallButton("Stop painting")) {
            director_->StopPropPainting();
        }
        PanelUi::Tooltip(PanelUi::kStopPaintingHelp);
    }
    RenderPropStripperControls_(1);
}

void FamiliesPanelTab::OpenNewFamily_() {
    newFamilyPopupOpen_ = true;
    std::strncpy(newFamilyName_, "New family", sizeof(newFamilyName_) - 1);
    newFamilyName_[sizeof(newFamilyName_) - 1] = '\0';
}

void FamiliesPanelTab::RenderFamilyTable_(const std::vector<FamilyRow>& rows, const float height) {
    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_ScrollY;
    if (!ImGui::BeginTable("FamiliesTable", 5, kFlags, ImVec2(0, height))) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, UI::typeColumnWidth());
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Instance ID", ImGuiTableColumnFlags_WidthFixed, UI::instanceIdColumnWidth());
    ImGui::TableSetupColumn("Props", ImGuiTableColumnFlags_WidthFixed, UI::propsColumnWidth());
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, UI::familyActionColWidth());
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const FamilyRow& row = rows[static_cast<size_t>(i)];
            ImGui::PushID(static_cast<int>(row.combinedIndex));
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Selectable("##familyrow", row.combinedIndex == selectedFamilyIndex_,
                              ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap |
                                  ImGuiSelectableFlags_AllowDoubleClick);
            if (ImGui::IsItemClicked()) {
                selectedFamilyIndex_ = row.combinedIndex;
                if (ImGui::IsMouseDoubleClicked(0)) {
                    QueuePaintForSelectedFamily_();
                }
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(row.isAuto ? "Built-in" : "Own");

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.family->name.c_str());

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(FormatFamilyId(row.familyId).c_str());

            ImGui::TableNextColumn();
            ImGui::Text("%zu", row.family->entries.size());

            ImGui::TableNextColumn();
            ImGui::BeginDisabled(row.family->entries.empty());
            if (ImGui::SmallButton("Paint")) {
                selectedFamilyIndex_ = row.combinedIndex;
                QueuePaintForSelectedFamily_();
            }
            ImGui::EndDisabled();
            if (row.family->entries.empty()) {
                Tooltip("This family has no props yet.");
            }
            ImGui::SameLine();
            const bool favorite = favorites_->IsFamilyFavorite(row.favoriteKey);
            if (ImGui::SmallButton(favorite ? "Unstar" : "Star")) {
                favorites_->ToggleFamilyFavorite(row.favoriteKey);
            }
            Tooltip(favorite ? "Remove from favorites" : "Add to favorites");

            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

void FamiliesPanelTab::RenderDetails_() {
    const PropFamily* family = GetSelectedFamily_();
    if (!family) {
        return;
    }
    const bool isAuto = IsSelectedAutoFamily_();
    const size_t autoCount = props_->GetAutoFamilies().size();
    const size_t userIndex = isAuto ? 0 : selectedFamilyIndex_ - autoCount;

    // The name. An own family can be renamed here: when the field is left, and not to a name in use.
    if (isAuto) {
        ImGui::TextUnformatted(family->name.c_str());
    }
    else {
        char name[128]{};
        std::snprintf(name, sizeof(name), "%s", renameFamilyIndex_ == userIndex ? renameFamilyName_.c_str() : family->name.c_str());
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##familyName", name, sizeof(name))) {
            renameFamilyIndex_ = userIndex;
            renameFamilyName_ = name;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && renameFamilyIndex_ == userIndex) {
            if (!renameFamilyName_.empty()) {
                favorites_->RenameUserFamily(userIndex, renameFamilyName_);
            }
            renameFamilyIndex_ = SIZE_MAX;
        }
        Tooltip("The name of this family. Click to change it.");
    }

    // What can be done with it comes before its props, so that it does not scroll out of view.
    const bool empty = family->entries.empty();
    ImGui::BeginDisabled(empty);
    if (ImGui::Button("Paint family")) {
        QueuePaintForSelectedFamily_();
    }
    ImGui::EndDisabled();
    if (empty) {
        Tooltip("This family has no props yet.");
    }
    if (!isAuto) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete family")) {
            ImGui::OpenPopup("deleteFamily");
        }
        if (PanelUi::ConfirmDeletePopup("deleteFamily", family->name.c_str())) {
            favorites_->DeleteUserFamily(userIndex);
            const size_t total = autoCount + favorites_->GetUserFamilies().size();
            if (selectedFamilyIndex_ >= total && total > 0) {
                selectedFamilyIndex_ = total - 1;
            }
            return;
        }
    }
    ImGui::SameLine(0.0f, kGroupGap);
    ImGui::AlignTextToFramePadding();
    const auto& autoFamilyIds = props_->GetAutoFamilyIds();
    if (isAuto && selectedFamilyIndex_ < autoFamilyIds.size()) {
        ImGui::Text("%zu props", family->entries.size());
        Tooltip("Built-in families cannot be changed.");
        ImGui::SameLine();
        ImGui::TextDisabled("0x%08X", autoFamilyIds[selectedFamilyIndex_]);
    }
    else {
        ImGui::Text("%zu props", family->entries.size());
    }
    if (RenderViewControls_()) {
        thumbnailCache_.Clear();
    }

    if (empty) {
        Hint(isAuto ? "This built-in family has no props."
                    : "This family is empty. In the Props tab, click ... on a prop, then choose Add to family.");
        return;
    }
    const float height = std::max(UI::iconRowHeight() * 3.0f + ImGui::GetTextLineHeightWithSpacing(), ImGui::GetContentRegionAvail().y);
    if (gridView_) {
        RenderMemberGrid_(*family, isAuto, userIndex, height);
    }
    else {
        RenderMemberTable_(*family, isAuto, userIndex, height);
    }
    thumbnailCache_.ProcessLoadQueue([this](const uint64_t key) {
        return LoadPropTexture_(key);
    });
}

void FamiliesPanelTab::RenderPropStar_(const Prop& prop) const {
    const bool favorite = favorites_->IsPropFavorite(prop.groupId.value(), prop.instanceId.value());
    if (ImGui::SmallButton(favorite ? "Unstar" : "Star")) {
        favorites_->TogglePropFavorite(prop.groupId.value(), prop.instanceId.value());
    }
    Tooltip(favorite ? "Remove this prop from favorites" : "Add this prop to favorites");
}

void FamiliesPanelTab::RenderMemberTable_(const PropFamily& family, const bool isAuto, const size_t userIndex, const float height) {
    if (!ImGui::BeginTable("FamilyEntries", isAuto ? 3 : 4,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, height))) {
        return;
    }
    const float actionsWidth = ButtonWidth("Unstar") + (isAuto ? 0.0f : ButtonWidth("x") + ImGui::GetStyle().ItemSpacing.x);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("##icon", ImGuiTableColumnFlags_WidthFixed, UI::iconColumnWidth());
    ImGui::TableSetupColumn("Prop", ImGuiTableColumnFlags_WidthStretch);
    if (!isAuto) {
        ImGui::TableSetupColumn("Weight", ImGuiTableColumnFlags_WidthFixed, UI::weightColumnWidth());
    }
    ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, actionsWidth);
    ImGui::TableHeadersRow();

    int removeIndex = -1;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(family.entries.size()), UI::iconRowHeight());
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const size_t i = static_cast<size_t>(row);
            const Prop* prop = props_->FindPropByInstanceId(family.entries[i].propID.value());

            ImGui::PushID(row);
            ImGui::TableNextRow(0, UI::iconRowHeight());

            ImGui::TableNextColumn();
            if (prop) {
                const uint64_t key = MakeGIKey(prop->groupId.value(), prop->instanceId.value());
                if (props_->GetPropThumbnailStore().HasThumbnail(key)) {
                    thumbnailCache_.Request(key);
                }
                RenderThumbnailWithPreview_(thumbnailCache_.Get(key), props_->GetPropThumbnailStore(), key);
            }
            else {
                RenderThumbnail_(std::nullopt);
            }

            ImGui::TableNextColumn();
            if (prop) {
                const auto renderBadges = [&]() {
                    bool renderedAnyBadge = false;
                    const SeasonalSet* seasonalSet = props_->FindSeasonalSetForProp(prop->instanceId.value());
                    Badges::ForEachBadge(*prop, seasonalSet, [&](const char* label, const ImU32 baseColor, const ImU32 hoverColor) {
                        if (renderedAnyBadge) {
                            ImGui::SameLine(0.0f, 4.0f);
                        }
                        Badges::RenderPill(label, baseColor, hoverColor);
                        renderedAnyBadge = true;
                    });
                    return renderedAnyBadge;
                };
                if (!prop->visibleName.empty()) {
                    ImGui::TextUnformatted(prop->visibleName.c_str());
                    if (renderBadges()) {
                        ImGui::SameLine(0.0f, 4.0f);
                    }
                    ImGui::TextDisabled("%s", prop->exemplarName.c_str());
                }
                else if (!prop->exemplarName.empty()) {
                    ImGui::TextUnformatted(prop->exemplarName.c_str());
                    renderBadges();
                }
                else {
                    ImGui::TextUnformatted("<unnamed prop>");
                }
            }
            else {
                ImGui::Text("Missing 0x%08X", family.entries[i].propID.value());
            }

            if (!isAuto) {
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::SliderFloat("##weight", &favorites_->GetUserFamilies()[userIndex].entries[i].weight, 0.1f, 10.0f, "%.1f");
                // Saved when let go, not on every frame of the drag.
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    favorites_->Save();
                }
                Tooltip("A higher weight places this prop more often.");
            }

            ImGui::TableNextColumn();
            if (prop) {
                RenderPropStar_(*prop);
            }
            if (!isAuto) {
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) {
                    removeIndex = row;
                }
                Tooltip("Remove from this family");
            }

            ImGui::PopID();
        }
    }
    ImGui::EndTable();

    if (removeIndex >= 0) {
        auto& entries = favorites_->GetUserFamilies()[userIndex].entries;
        entries.erase(entries.begin() + removeIndex);
        favorites_->Save();
    }
}

void FamiliesPanelTab::OnDeviceReset(const uint32_t deviceGeneration) {
    if (deviceGeneration != lastDeviceGeneration_) {
        thumbnailCache_.OnDeviceReset();
        previewCache_.OnDeviceReset();
        lastDeviceGeneration_ = deviceGeneration;
    }
}

ImGuiTexture FamiliesPanelTab::LoadPropTexture_(const uint64_t propKey) const {
    ImGuiTexture texture;

    if (!imguiService_) {
        return texture;
    }

    auto data = props_->GetPropThumbnailStore().LoadThumbnail(propKey, static_cast<uint32_t>(ShownThumbnailSize_()));
    if (!data.has_value()) {
        return texture;
    }

    texture.Create(imguiService_, data->width, data->height, data->rgba.data());
    return texture;
}

void FamiliesPanelTab::RenderMemberGrid_(const PropFamily& family, const bool isAuto, const size_t userIndex, const float height) {
    if (!ImGui::BeginChild("FamilyEntriesGrid", ImVec2(0, height), false,
                           ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        ImGui::EndChild();
        return;
    }

    ThumbnailStore& store = props_->GetPropThumbnailStore();
    int removeIndex = -1;
    const size_t visible = ThumbnailGrid::Render(family.entries.size(), gridTileSize_,
        [&](const size_t index) { return family.entries[index].propID.value(); },
        [&](const size_t index) {
        const FamilyEntry& entry = family.entries[index];
        const Prop* prop = props_->FindPropByInstanceId(entry.propID.value());
        const uint64_t key = prop ? MakeGIKey(prop->groupId.value(), prop->instanceId.value()) : 0;
        const bool hasThumbnail = prop && store.HasThumbnail(key);

        std::optional<void*> texture;
        if (hasThumbnail) {
            texture = thumbnailCache_.Get(key);
            if (!texture) {
                thumbnailCache_.Request(key);
            }
        }
        RenderGridTile_(texture);
        if (!hasThumbnail) {
            char missing[24];
            std::snprintf(missing, sizeof(missing), "0x%08X", entry.propID.value());
            ThumbnailGrid::LabelLastTile(prop ? (prop->visibleName.empty() ? prop->exemplarName.c_str()
                                                                           : prop->visibleName.c_str())
                                              : missing);
        }
        const bool favorite = prop && favorites_->IsPropFavorite(prop->groupId.value(), prop->instanceId.value());
        ThumbnailGrid::DecorateLastTile(false, favorite);

        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            if (prop) {
                ImGui::TextUnformatted(prop->visibleName.empty() ? prop->exemplarName.c_str() : prop->visibleName.c_str());
                if (!prop->visibleName.empty()) {
                    ImGui::TextDisabled("%s", prop->exemplarName.c_str());
                }
            }
            else {
                ImGui::Text("Missing 0x%08X", entry.propID.value());
            }
            if (!isAuto) {
                ImGui::Text("Weight %.1f", entry.weight);
            }
            if (prop || !isAuto) {
                ImGui::TextDisabled("Right-click for more.");
            }
            if (hasThumbnail) {
                RenderLargePreview_(store, key);
            }
            ImGui::EndTooltip();
        }

        if ((prop || !isAuto) && ImGui::BeginPopupContextItem("##FamilyTileMenu")) {
            if (prop && ImGui::Selectable(favorite ? "Remove from favorites" : "Add to favorites")) {
                favorites_->TogglePropFavorite(prop->groupId.value(), prop->instanceId.value());
            }
            if (!isAuto) {
                ImGui::SetNextItemWidth(10.0f * ImGui::GetFontSize());
                ImGui::SliderFloat("Weight", &favorites_->GetUserFamilies()[userIndex].entries[index].weight, 0.1f, 10.0f, "%.1f");
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    favorites_->Save();
                }
                if (ImGui::Selectable("Remove from family")) {
                    removeIndex = static_cast<int>(index);
                }
            }
            ImGui::EndPopup();
        }
    });
    thumbnailCache_.FitVisible(visible + Cache::kMaxLoadPerFrame);
    ImGui::EndChild();

    if (removeIndex >= 0) {
        auto& entries = favorites_->GetUserFamilies()[userIndex].entries;
        entries.erase(entries.begin() + removeIndex);
        favorites_->Save();
    }
}

void FamiliesPanelTab::RenderNewFamilyPopup_() {
    if (!newFamilyPopupOpen_) {
        return;
    }

    if (!ImGui::IsPopupOpen("New family")) {
        ImGui::OpenPopup("New family");
    }
    if (ImGui::BeginPopupModal("New family", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        const bool entered = ImGui::InputText("Name", newFamilyName_, sizeof(newFamilyName_), ImGuiInputTextFlags_EnterReturnsTrue);

        const bool empty = newFamilyName_[0] == '\0';
        const bool taken = std::ranges::any_of(favorites_->GetUserFamilies(), [this](const PropFamily& family) {
            return family.name == newFamilyName_;
        });
        ImGui::BeginDisabled(empty || taken);
        const bool create = ImGui::Button("Create");
        ImGui::EndDisabled();
        if (empty || taken) {
            Tooltip(empty ? "First type a name." : "A family with this name already exists.");
        }
        if ((create || entered) && !empty && !taken) {
            if (favorites_->CreateUserFamily(newFamilyName_)) {
                // Select the newly created family, and make sure that the list shows it.
                selectedFamilyIndex_ = props_->GetAutoFamilies().size() + favorites_->GetUserFamilies().size() - 1;
                ClearFilters_();
            }
            newFamilyPopupOpen_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel") || (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
            newFamilyPopupOpen_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void FamiliesPanelTab::QueuePaintForSelectedFamily_() {
    const PropFamily* family = GetSelectedFamily_();
    if (!family || family->entries.empty()) {
        return;
    }

    const bool isAutoFamily = IsSelectedAutoFamily_();
    uint64_t sourceId = 0;
    RecentPaintEntry::SourceKind sourceKind = RecentPaintEntry::SourceKind::PropAutoFamily;
    if (isAutoFamily) {
        const auto& autoFamilyIds = props_->GetAutoFamilyIds();
        if (selectedFamilyIndex_ < autoFamilyIds.size()) {
            sourceId = autoFamilyIds[selectedFamilyIndex_];
        }
    }
    else {
        sourceKind = RecentPaintEntry::SourceKind::PropUserFamily;
        sourceId = family->persistentId.has_value() ? family->persistentId->value() : 0;
    }

    pendingPaint_.fallbackPropId = family->entries.front().propID.value();
    pendingPaint_.sourceKind = sourceKind;
    pendingPaint_.sourceId = sourceId;
    pendingPaint_.familyName = family->name;
    pendingPaint_.settings = familyPaintDefaults_;
    pendingPaint_.settings.activePalette = family->entries;
    pendingPaint_.settings.randomSeed = 0;
    pendingPaint_.open = true;
}

void FamiliesPanelTab::RenderPaintOptionsPopup_() {
    if (pendingPaint_.open) {
        director_->ApplyGridChoice(&PaintGridChoices::props, pendingPaint_.settings);
        ImGui::OpenPopup("Paint family");
        pendingPaint_.open = false;
    }

    PanelUi::LimitDialogHeight();
    if (ImGui::BeginPopupModal("Paint family", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Family: %s", pendingPaint_.familyName.c_str());
        ImGui::Separator();

        RenderPaintOptions_(pendingPaint_.settings, "props", true);

        // Kept for the next family, also when this one is cancelled.
        familyPaintDefaults_ = pendingPaint_.settings;
        familyPaintDefaults_.activePalette.clear();
        familyPaintDefaults_.randomSeed = 0;

        ImGui::Separator();
        if (RenderStartCancel_()) {
            StartPaintingWithSelectedFamily_();
        }

        ImGui::EndPopup();
    }
}

bool FamiliesPanelTab::StartPaintingWithSelectedFamily_() {
    if (pendingPaint_.fallbackPropId == 0 || pendingPaint_.settings.activePalette.empty()) {
        return false;
    }

    ReleaseImGuiInputCapture_();
    director_->SaveGridChoice(&PaintGridChoices::props, pendingPaint_.settings);

    PropPaintSettings settings = pendingPaint_.settings;
    if (settings.randomSeed == 0) {
        settings.randomSeed = static_cast<uint32_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
    }

    const RecentPaintSource source{
        .sourceKind = pendingPaint_.sourceKind,
        .sourceId = pendingPaint_.sourceId
    };
    return director_->StartPropPainting(pendingPaint_.fallbackPropId, settings, pendingPaint_.familyName, source);
}

const PropFamily* FamiliesPanelTab::GetSelectedFamily_() const {
    const auto& autoFamilies = props_->GetAutoFamilies();
    const auto& userFamilies = favorites_->GetUserFamilies();
    const size_t autoCount = autoFamilies.size();
    const size_t userCount = userFamilies.size();

    if (selectedFamilyIndex_ < autoCount) {
        return &autoFamilies[selectedFamilyIndex_];
    }
    const size_t userIndex = selectedFamilyIndex_ - autoCount;
    if (userIndex < userCount) {
        return &userFamilies[userIndex];
    }
    return nullptr;
}

bool FamiliesPanelTab::IsSelectedAutoFamily_() const {
    return selectedFamilyIndex_ < props_->GetAutoFamilies().size();
}
