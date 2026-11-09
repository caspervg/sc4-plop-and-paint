#include "PropPanelTab.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include "../../shared/SeasonalSetDetector.hpp"
#include "../common/BadgeUtils.hpp"
#include "../common/Constants.hpp"
#include "../common/Utils.hpp"
#include "../packs/PackCatalogRepository.hpp"
#include "../utils/Logger.h"

using PanelUi::ButtonWidth;
using PanelUi::CheckboxWidth;
using PanelUi::Hint;
using PanelUi::kGroupGap;
using PanelUi::NextInRow;
using PanelUi::Tooltip;

namespace {
    const char* MonthName_(uint8_t month) {
        static constexpr const char* kMonths[] = {
            "January", "February", "March", "April", "May", "June",
            "July", "August", "September", "October", "November", "December"
        };

        if (month >= 1 && month <= 12) {
            return kMonths[month - 1];
        }

        return "Unknown";
    }

    void FormatHour_(const float hourValue, char* buffer, const size_t bufferSize) {
        int totalMinutes = static_cast<int>(hourValue * 60.0f + 0.5f);
        totalMinutes %= 24 * 60;
        if (totalMinutes < 0) {
            totalMinutes += 24 * 60;
        }

        const int hours = totalMinutes / 60;
        const int minutes = totalMinutes % 60;
        std::snprintf(buffer, bufferSize, "%02d:%02d", hours, minutes);
    }

    std::string PropDisplayName_(const Prop& prop) {
        if (!prop.visibleName.empty()) {
            return prop.visibleName;
        }
        if (!prop.exemplarName.empty()) {
            return prop.exemplarName;
        }
        return "<unnamed prop>";
    }

    std::string DateWindowText_(const Prop& prop) {
        if (!prop.simulatorDateStart.has_value()) {
            return "all year";
        }
        char buffer[32]{};
        std::snprintf(buffer, sizeof(buffer), "%u/%u +%ud",
                      prop.simulatorDateStart->month, prop.simulatorDateStart->day,
                      prop.simulatorDateDuration.value_or(0));
        return buffer;
    }

}

const char* PropPanelTab::GetTabName() const {
    return "Props";
}

void PropPanelTab::OnRender() {
    const auto& props = props_->GetProps();

    if (props.empty()) {
        ImGui::TextWrapped("No props loaded. Run the cache builder to make props.cbor in the Plugins folder, "
                           "then restart the game.");
        return;
    }

    RenderFilterUI_();

    ImGui::Separator();
    EnsureFilteredPropsCache_();

    RenderResultLine_();
    RenderTools_();

    if (filteredPropsCache_.empty()) {
        if (PanelUi::NoResults("No props match these filters.")) {
            ClearFilters_();
        }
    }
    // Table in scrollable child region so filters stay visible
    else if (gridView_) {
        if (ImGui::BeginChild("PropGridRegion", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
            RenderGrid_(filteredPropsCache_);
        }
        ImGui::EndChild();
    }
    else {
        if (ImGui::BeginChild("PropTableRegion", ImVec2(0, 0), false)) {
            RenderTableInternal_(filteredPropsCache_, favorites_->GetFavoritePropIds());
        }
        ImGui::EndChild();
    }

    RenderRotationModal_();
    RenderSeasonalSetEditor_();
}

void PropPanelTab::RenderResultLine_() {
    ImGui::Text("Showing %zu of %zu props", filteredPropsCache_.size(), allPropViewsCache_.size());
    const int active = filterHelper_.ActiveFilterCount();
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
    Tooltip(active == 0 ? "No filters are on." : "Show all props again.");
    if (RenderViewControls_()) {
        thumbnailCache_.Clear();
    }
}

void PropPanelTab::ClearFilters_() {
    filterHelper_.ResetFilters();
    packFilter_.Clear();
    filteredPropsDirty_ = true;
}

void PropPanelTab::RenderTools_() {
    size_t index = 0;
    if (director_->IsPropPainting()) {
        if (ImGui::SmallButton("Stop painting")) {
            director_->StopPropPainting();
        }
        ++index;
    }
    if (director_->IsPropPicking()) {
        NextInRow(index, ButtonWidth("Stop picking"));
        if (ImGui::SmallButton("Stop picking##PropPicker")) {
            director_->StopPropPicking();
        }
    }
    else {
        NextInRow(index, ButtonWidth("Pick prop"));
        const bool hasAnySource =
            (director_->GetPropStripperSources() &
                (PropStripperInputControl::SourceFlagCity |
                 PropStripperInputControl::SourceFlagLot |
                 PropStripperInputControl::SourceFlagStreet)) != 0;
        ImGui::BeginDisabled(!hasAnySource);
        if (ImGui::SmallButton("Pick prop")) {
            ReleaseImGuiInputCapture_();
            director_->StartPropPicking([this](const PickedProp& picked) {
                HandlePickedProp_(picked);
            });
        }
        ImGui::EndDisabled();
        Tooltip(hasAnySource ? "Click a city, lot or street prop to select it here." : "Turn on City, Lot or Street first.");
    }
    RenderPropStripperControls_(1);
}

void PropPanelTab::OnDeviceReset(const uint32_t deviceGeneration) {
    if (deviceGeneration != lastDeviceGeneration_) {
        thumbnailCache_.OnDeviceReset();
        previewCache_.OnDeviceReset();
        lastDeviceGeneration_ = deviceGeneration;
    }
}

ImGuiTexture PropPanelTab::LoadPropTexture_(uint64_t propKey) {
    ImGuiTexture texture;

    if (!imguiService_) {
        LOG_WARN("Could not load prop thumbnail: imguiService_ is null");
        return texture;
    }

    auto data = props_->GetPropThumbnailStore().LoadThumbnail(propKey, static_cast<uint32_t>(ShownThumbnailSize_()));
    if (!data.has_value()) {
        LOG_WARN("Prop thumbnail not found in store for key 0x{:016X}", propKey);
        return texture;
    }

    texture.Create(imguiService_, data->width, data->height, data->rgba.data());
    return texture;
}

void PropPanelTab::RenderFilterUI_() {
    static char searchBuf[256] = {};
    if (filterHelper_.searchBuffer != searchBuf) {
        std::strncpy(searchBuf, filterHelper_.searchBuffer.c_str(), sizeof(searchBuf) - 1);
        searchBuf[sizeof(searchBuf) - 1] = '\0';
    }

    ImGui::SetNextItemWidth(-1);
    bool filtersChanged = false;

    if (ImGui::InputTextWithHint("##SearchProps", "Search props...", searchBuf, sizeof(searchBuf))) {
        filterHelper_.searchBuffer = searchBuf;
        filtersChanged = true;
    }

    filtersChanged = packFilter_.Render("##PropPack", "props") || filtersChanged;

    const struct {
        const char* label;
        bool* value;
        const char* tip;
    } kToggles[] = {
        {"Favorites only", &filterHelper_.favoritesOnly, "Show only the props that you starred."},
        {"In a family", &filterHelper_.requireFamily, "Show only props that are in a prop family."},
        {"Day/Night", &filterHelper_.requireDayNight, "Show only props that look different at night."},
        {"Timed", &filterHelper_.requireTimed, "Show only props that are visible at certain hours."},
        {"Seasonal", &filterHelper_.requireSeasonal, "Show only props that are visible on certain dates."},
        {"<100% Chance", &filterHelper_.requireReducedChance, "Show only props that do not always appear."},
    };
    size_t toggleIndex = 0;
    for (const auto& toggle : kToggles) {
        NextInRow(toggleIndex++, CheckboxWidth(toggle.label));
        filtersChanged = ImGui::Checkbox(toggle.label, toggle.value) || filtersChanged;
        Tooltip(toggle.tip);
    }

    // Each label stays with its slider when the row wraps.
    const struct {
        const char* label;
        const char* id;
        float* range;
    } kSizes[] = {
        {"Width", "##PropWidth", filterHelper_.propWidth},
        {"Height", "##PropHeight", filterHelper_.propHeight},
        {"Depth", "##PropDepth", filterHelper_.propDepth},
    };
    size_t sizeIndex = 0;
    for (const auto& size : kSizes) {
        NextInRow(sizeIndex++, ImGui::CalcTextSize(size.label).x + ImGui::GetStyle().ItemSpacing.x + UI::wideInputWidth(), kGroupGap);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(size.label);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(UI::wideInputWidth());
        filtersChanged = ImGui::SliderFloat2(size.id, size.range, PropSize::kMinSize, PropSize::kMaxSize, UI::kMeterFloatFormat) || filtersChanged;
    }

    if (filtersChanged) {
        filteredPropsDirty_ = true;
    }
}

void PropPanelTab::RenderTable_() {
    EnsureFilteredPropsCache_();
    RenderTableInternal_(filteredPropsCache_, favorites_->GetFavoritePropIds());
}

void PropPanelTab::RebuildPropViewsCache_() {
    const auto& props = props_->GetProps();
    const PackCatalogRepository* catalog = director_->GetPackCatalog();
    filterHelper_.packFilter = &packFilter_;
    packFilter_.Begin(catalog);
    allPropViewsCache_.clear();
    allPropViewsCache_.reserve(props.size());
    for (const auto& prop : props) {
        const auto rows = catalog ? catalog->ExemplarRows(prop.groupId.value(), prop.instanceId.value())
                                  : std::span<const PackCatalogRepository::ExemplarRow>{};
        packFilter_.Count(rows);
        allPropViewsCache_.push_back(PropView{&prop, rows});
    }
    cachedPropCount_ = props.size();
    filteredPropsDirty_ = true;
}

void PropPanelTab::EnsureFilteredPropsCache_() {
    const auto& props = props_->GetProps();
    if (cachedPropCount_ != props.size() || allPropViewsCache_.size() != props.size()) {
        RebuildPropViewsCache_();
    }

    const size_t favoriteCount = filterHelper_.favoritesOnly ? favorites_->GetFavoritePropIds().size() : 0;
    if (favoriteCount != appliedFavoriteCount_) {
        appliedFavoriteCount_ = favoriteCount;
        filteredPropsDirty_ = true;
    }

    if (!filteredPropsDirty_) {
        return;
    }

    filteredPropsCache_ = filterHelper_.ApplyFiltersAndSort(
        allPropViewsCache_, favorites_->GetFavoritePropIds(), sortSpecs_);
    filteredPropsDirty_ = false;
}

void PropPanelTab::RenderTableInternal_(const std::vector<PropView>& filteredProps,
                                        const std::unordered_set<uint64_t>& favorites) {
    (void)favorites;

    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBody |
        ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTable("PropsTable", 4, tableFlags, ImVec2(0, 0))) {
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
            if (!newSpecs.empty() && newSpecs != sortSpecs_) {
                sortSpecs_ = std::move(newSpecs);
                filteredPropsDirty_ = true;
            }
            specs->SpecsDirty = false;
        }

        const float rowHeight = UI::iconRowHeight();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(filteredProps.size()), rowHeight);

        while (clipper.Step()) {
            // Request texture loads for visible and margin items
            const int prefetchStart = std::max(0, clipper.DisplayStart - Cache::kPrefetchMargin);
            const int prefetchEnd = std::min(static_cast<int>(filteredProps.size()), clipper.DisplayEnd + Cache::kPrefetchMargin);
            for (int i = prefetchStart; i < prefetchEnd; ++i) {
                const auto& prop = *filteredProps[i].prop;
                const uint64_t key = MakeGIKey(prop.groupId.value(), prop.instanceId.value());
                if (props_->GetPropThumbnailStore().HasThumbnail(key)) {
                    thumbnailCache_.Request(key);
                }
            }

            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto& prop = *filteredProps[i].prop;
                const uint64_t key = MakeGIKey(prop.groupId.value(), prop.instanceId.value());

                ImGui::PushID(static_cast<int>(key));
                ImGui::TableNextRow(0, rowHeight);

                ImGui::TableNextColumn();
                // As on the grid: a click selects (and switches the prop while painting), a
                // double-click paints, a right-click opens the menu.
                if (ImGui::Selectable("##row", key == selectedPropKey_,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap |
                                      ImGuiSelectableFlags_AllowDoubleClick,
                                      ImVec2(0, rowHeight))) {
                    selectedPropKey_ = key;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || director_->IsPropPainting()) {
                        PaintProp_(prop);
                    }
                }
                if (ImGui::BeginPopupContextItem("##PropMenu")) {
                    RenderPropMenu_(prop);
                    ImGui::EndPopup();
                }
                ImGui::SameLine();
                auto thumbTextureId = thumbnailCache_.Get(key);
                RenderThumbnailWithPreview_(thumbTextureId, props_->GetPropThumbnailStore(), key);

                // Name, its badges and its exemplar name. The row's selectable spans this cell, so
                // the tooltip tests the rectangle rather than the items.
                ImGui::TableNextColumn();
                ImGui::BeginGroup();
                ImGui::TextUnformatted(prop.visibleName.empty() ? prop.exemplarName.c_str() : prop.visibleName.c_str());
                const bool hasPills = RenderPropPills_(prop, true);
                if (!prop.visibleName.empty()) {
                    if (hasPills) {
                        ImGui::SameLine(0.0f, 4.0f);
                    }
                    ImGui::TextDisabled("%s", prop.exemplarName.c_str());
                }
                ImGui::EndGroup();
                if (HasPropTooltipContent_(prop) && ImGui::IsWindowHovered() &&
                    ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax())) {
                    RenderPropTooltip_(prop);
                }

                // Size
                ImGui::TableNextColumn();
                ImGui::Text("%.1f x %.1f x %.1f", prop.width, prop.height, prop.depth);

                // Actions
                ImGui::TableNextColumn();
                if (ImGui::Button("Paint")) {
                    PaintProp_(prop);
                }
                ImGui::SameLine();
                RenderFavButton_(prop);
                ImGui::SameLine();
                if (ImGui::Button("...")) {
                    ImGui::OpenPopup("##PropMenu");
                }
                Tooltip("Add to a family or a seasonal set.");

                ImGui::PopID();
            }
        }

        thumbnailCache_.ProcessLoadQueue([this](const uint64_t key) {
            return LoadPropTexture_(key);
        });

        ImGui::EndTable();
    }
}

void PropPanelTab::RenderFavButton_(const Prop& prop) const {
    const bool isFavorite = favorites_->IsPropFavorite(prop.groupId.value(), prop.instanceId.value());
    if (ImGui::Button(isFavorite ? "Unstar" : "Star")) {
        favorites_->TogglePropFavorite(prop.groupId.value(), prop.instanceId.value());
        if (filterHelper_.favoritesOnly) {
            const_cast<PropPanelTab*>(this)->filteredPropsDirty_ = true;
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(isFavorite ? "Remove from favorites" : "Add to favorites");
    }
}

void PropPanelTab::RenderPropMenu_(const Prop& prop) {
    selectedPropKey_ = MakeGIKey(prop.groupId.value(), prop.instanceId.value());
    if (ImGui::Selectable("Paint")) {
        PaintProp_(prop);
    }
    const bool isFavorite = favorites_->IsPropFavorite(prop.groupId.value(), prop.instanceId.value());
    if (ImGui::Selectable(isFavorite ? "Remove from favorites" : "Add to favorites")) {
        favorites_->TogglePropFavorite(prop.groupId.value(), prop.instanceId.value());
        filteredPropsDirty_ = filteredPropsDirty_ || filterHelper_.favoritesOnly;
    }
    if (ImGui::BeginMenu("Add to family")) {
        RenderAddToFamilyItems_(prop);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Seasonal set")) {
        RenderSeasonalSetItems_(prop);
        ImGui::EndMenu();
    }
}

void PropPanelTab::RenderSeasonalSetItems_(const Prop& prop) {
    const uint32_t propId = prop.instanceId.value();
    const SeasonalSet* currentSet = props_->FindSeasonalSetForProp(propId);
    if (currentSet != nullptr && currentSet->confidence != 2) {
        char label[96]{};
        std::snprintf(label, sizeof(label), "Convert detected set '%s' to custom", currentSet->name.c_str());
        if (ImGui::Selectable(label)) {
            ConvertAutoSetToUserSet_(*currentSet);
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
    }

    const auto& userSets = favorites_->GetUserSeasonalSets();
    for (size_t setIndex = 0; setIndex < userSets.size(); ++setIndex) {
        if (ImGui::Selectable(userSets[setIndex].name.c_str())) {
            if (favorites_->AddPropToUserSeasonalSet(propId, setIndex)) {
                ApplyUserSeasonalSetsToRepository_();
            }
            ImGui::CloseCurrentPopup();
            break;
        }
    }
    if (!userSets.empty()) {
        ImGui::Separator();
    }

    if (ImGui::Selectable("New custom set from this prop")) {
        AddPropToNewSeasonalSet_(prop);
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::Selectable("Manage seasonal sets...")) {
        seasonalSetEditorOpen_ = true;
        ImGui::CloseCurrentPopup();
    }
}

void PropPanelTab::RenderAddToFamilyItems_(const Prop& prop) {
    const auto& userFamilies = favorites_->GetUserFamilies();
    for (size_t familyIndex = 0; familyIndex < userFamilies.size(); ++familyIndex) {
        if (ImGui::Selectable(userFamilies[familyIndex].name.c_str())) {
            favorites_->AddPropToUserFamily(prop.instanceId.value(), familyIndex);
            ImGui::CloseCurrentPopup();
            return;
        }
    }
    if (!userFamilies.empty()) {
        ImGui::Separator();
    }
    if (ImGui::Selectable("New family from this prop")) {
        favorites_->AddPropToNewUserFamily(prop.instanceId.value(), PropDisplayName_(prop));
        ImGui::CloseCurrentPopup();
    }
}

void PropPanelTab::PaintProp_(const Prop& prop) {
    selectedPropKey_ = MakeGIKey(prop.groupId.value(), prop.instanceId.value());
    if (director_->IsPropPainting()) {
        ReleaseImGuiInputCapture_();
        // Keeps the current paint mode and rotation; no modal.
        if (director_->SwitchPropPaintingTarget(prop.instanceId.value(), PropDisplayName_(prop))) {
            return;
        }
    }
    pendingPaint_.propId = prop.instanceId.value();
    pendingPaint_.propName = PropDisplayName_(prop);
    pendingPaint_.settings.mode = PaintMode::Direct;
    pendingPaint_.settings.rotation = 0;
    pendingPaint_.open = true;
}

void PropPanelTab::RenderGrid_(const std::vector<PropView>& filteredProps) {
    ThumbnailStore& store = props_->GetPropThumbnailStore();
    const size_t visible = ThumbnailGrid::Render(filteredProps.size(), gridTileSize_,
        [&](const size_t index) {
            const Prop& prop = *filteredProps[index].prop;
            return MakeGIKey(prop.groupId.value(), prop.instanceId.value());
        },
        [&](const size_t index) {
        const Prop& prop = *filteredProps[index].prop;
        const uint64_t key = MakeGIKey(prop.groupId.value(), prop.instanceId.value());
        const bool hasThumbnail = store.HasThumbnail(key);

        const auto texture = thumbnailCache_.Get(key);
        if (!texture && hasThumbnail) {
            thumbnailCache_.Request(key);
        }
        RenderGridTile_(texture);
        if (!hasThumbnail) {
            ThumbnailGrid::LabelLastTile(PropDisplayName_(prop).c_str());
        }
        ThumbnailGrid::DecorateLastTile(key == selectedPropKey_,
                                        favorites_->IsPropFavorite(prop.groupId.value(), prop.instanceId.value()));

        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(PropDisplayName_(prop).c_str());
            if (!prop.visibleName.empty()) {
                ImGui::TextDisabled("%s", prop.exemplarName.c_str());
            }
            ImGui::Text("%.1f x %.1f x %.1f m", prop.width, prop.height, prop.depth);
            RenderLargePreview_(store, key);
            if (HasPropTooltipContent_(prop)) {
                ImGui::Separator();
                RenderPropTooltipBody_(prop);
            }
            ImGui::EndTooltip();
        }

        // Click selects (and switches the prop while painting), double-click paints.
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            selectedPropKey_ = key;
            if (ImGui::GetMouseClickedCount(ImGuiMouseButton_Left) >= 2 || director_->IsPropPainting()) {
                PaintProp_(prop);
            }
        }

        if (ImGui::BeginPopupContextItem("##PropTileMenu")) {
            RenderPropMenu_(prop);
            ImGui::EndPopup();
        }
    });
    thumbnailCache_.FitVisible(visible + Cache::kMaxLoadPerFrame);

    thumbnailCache_.ProcessLoadQueue([this](const uint64_t key) {
        return LoadPropTexture_(key);
    });
}

void PropPanelTab::ApplyUserSeasonalSetsToRepository_() const {
    props_->ApplyUserSeasonalSets(favorites_->GetUserSeasonalSets());
}

void PropPanelTab::AddPropToNewSeasonalSet_(const Prop& prop) {
    std::string baseName = seasonal::detail::PrettifyStem(seasonal::detail::Stem(prop.exemplarName));
    if (baseName.empty()) {
        baseName = PropDisplayName_(prop);
    }

    std::string candidateName = baseName;
    int suffix = 2;
    while (!favorites_->CreateUserSeasonalSet(candidateName)) {
        candidateName = baseName + " (" + std::to_string(suffix++) + ")";
        if (suffix > 20) {
            return;
        }
    }
    favorites_->AddPropToUserSeasonalSet(prop.instanceId.value(), favorites_->GetUserSeasonalSets().size() - 1);
    ApplyUserSeasonalSetsToRepository_();
    seasonalSetEditorOpen_ = true;
}

void PropPanelTab::ConvertAutoSetToUserSet_(const SeasonalSet& autoSet) {
    // Copy before mutating favorites: autoSet points into the repository's merged
    // view, which ApplyUserSeasonalSets rebuilds.
    const std::string baseName = autoSet.name;
    const std::vector<rfl::Hex<uint32_t>> members = autoSet.members;

    std::string candidateName = baseName;
    int suffix = 2;
    while (!favorites_->CreateUserSeasonalSet(candidateName)) {
        candidateName = baseName + " (" + std::to_string(suffix++) + ")";
        if (suffix > 20) {
            return;
        }
    }
    const size_t setIndex = favorites_->GetUserSeasonalSets().size() - 1;
    for (const auto& member : members) {
        favorites_->AddPropToUserSeasonalSet(member.value(), setIndex);
    }
    ApplyUserSeasonalSetsToRepository_();
    seasonalSetEditorOpen_ = true;
}

std::vector<const Prop*> PropPanelTab::SuggestSeasonalSetMembers_(const SeasonalSet& set) const {
    namespace sd = seasonal::detail;

    std::vector<const Prop*> memberProps;
    std::vector<sd::DateWindow> memberWindows;
    for (const auto& member : set.members) {
        if (const Prop* memberProp = props_->FindPropByInstanceId(member.value())) {
            memberProps.push_back(memberProp);
            if (memberProp->simulatorDateStart.has_value()) {
                memberWindows.push_back(sd::WindowOf(*memberProp));
            }
        }
    }
    if (memberProps.empty()) {
        return {};
    }

    const uint32_t groupId = memberProps.front()->groupId.value();
    const auto isMember = [&set](const uint32_t instanceId) {
        return std::any_of(set.members.begin(), set.members.end(),
            [instanceId](const rfl::Hex<uint32_t>& member) { return member.value() == instanceId; });
    };

    std::vector<const Prop*> suggestions;
    for (const auto& prop : props_->GetProps()) {
        if (prop.groupId.value() != groupId || isMember(prop.instanceId.value())) {
            continue;
        }
        if (!prop.simulatorDateStart.has_value() ||
            prop.simulatorDateDuration.value_or(0) < seasonal::kMinSeasonDays) {
            continue;
        }
        const SeasonalSet* existing = props_->FindSeasonalSetForProp(prop.instanceId.value());
        if (existing != nullptr && existing->confidence == 2) {
            continue;
        }

        const auto window = sd::WindowOf(prop);
        const bool windowFits = std::all_of(memberWindows.begin(), memberWindows.end(),
            [&window](const sd::DateWindow& memberWindow) {
                return sd::OverlapDays(window, memberWindow) <= seasonal::kMaxRescueOverlapDays;
            });
        if (!windowFits) {
            continue;
        }

        const bool nameFits = std::any_of(memberProps.begin(), memberProps.end(),
            [&prop](const Prop* memberProp) {
                const size_t prefix = sd::CommonPrefixLength(prop.exemplarName, memberProp->exemplarName);
                const size_t minLength = std::min(prop.exemplarName.size(), memberProp->exemplarName.size());
                return prefix >= 6 && prefix * 2 >= minLength;
            });
        if (!nameFits) {
            continue;
        }

        suggestions.push_back(&prop);
        if (suggestions.size() >= 8) {
            break;
        }
    }
    return suggestions;
}

void PropPanelTab::RenderSeasonalSetEditor_() {
    if (!seasonalSetEditorOpen_) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(540, 460), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Seasonal sets###PlopAndPaintSeasonalSets", &seasonalSetEditorOpen_, ImGuiWindowFlags_NoNav)) {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped("You paint and remove a custom seasonal set as one stack. "
                       "A custom set replaces any automatic set that has a member in common.");
    const auto& userSets = favorites_->GetUserSeasonalSets();
    ImGui::InputTextWithHint("##NewSetName", "New set name", newSeasonalSetName_, sizeof(newSeasonalSetName_));
    ImGui::SameLine();
    const bool nameEmpty = newSeasonalSetName_[0] == '\0';
    const bool nameTaken = std::ranges::any_of(userSets, [&](const SeasonalSet& set) { return set.name == newSeasonalSetName_; });
    ImGui::BeginDisabled(nameEmpty || nameTaken);
    if (ImGui::Button("Create")) {
        if (favorites_->CreateUserSeasonalSet(newSeasonalSetName_)) {
            newSeasonalSetName_[0] = '\0';
            ApplyUserSeasonalSetsToRepository_();
        }
    }
    ImGui::EndDisabled();
    if (nameEmpty || nameTaken) {
        Tooltip(nameEmpty ? "First type a name." : "A set with this name already exists.");
    }
    ImGui::Separator();

    if (userSets.empty()) {
        Hint("No custom sets yet. To start one, click ... on a prop in the list, then choose Seasonal set.");
    }

    struct PendingAction {
        enum class Kind { AddMember, RemoveMember, Rename, DeleteSet } kind;
        size_t setIndex = 0;
        uint32_t propId = 0;
        std::string name;
    };
    std::optional<PendingAction> pending;

    for (size_t setIndex = 0; setIndex < userSets.size(); ++setIndex) {
        const auto& set = userSets[setIndex];
        ImGui::PushID(static_cast<int>(setIndex));

        char header[96]{};
        std::snprintf(header, sizeof(header), "%s (%zu member%s)###SetHeader",
                      set.name.c_str(), set.members.size(), set.members.size() == 1 ? "" : "s");
        if (ImGui::CollapsingHeader(header)) {
            // Renamed when the field is left. A name already in use is not taken.
            char setName[64]{};
            std::snprintf(setName, sizeof(setName), "%s",
                          renameSeasonalIndex_ == setIndex ? renameSeasonalName_.c_str() : set.name.c_str());
            if (ImGui::InputText("##SetName", setName, sizeof(setName))) {
                renameSeasonalIndex_ = setIndex;
                renameSeasonalName_ = setName;
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && renameSeasonalIndex_ == setIndex) {
                if (!renameSeasonalName_.empty()) {
                    pending = PendingAction{PendingAction::Kind::Rename, setIndex, 0, renameSeasonalName_};
                }
                renameSeasonalIndex_ = SIZE_MAX;
            }
            Tooltip("The name of this set. Click to change it.");
            ImGui::SameLine(0.0f, kGroupGap);
            if (ImGui::Button("Delete set")) {
                ImGui::OpenPopup("deleteSet");
            }
            if (PanelUi::ConfirmDeletePopup("deleteSet", set.name.c_str())) {
                pending = PendingAction{PendingAction::Kind::DeleteSet, setIndex, 0, {}};
            }

            // Year coverage over the members that carry date windows.
            std::array<uint8_t, 365> coverage{};
            for (const auto& member : set.members) {
                if (const Prop* memberProp = props_->FindPropByInstanceId(member.value());
                    memberProp != nullptr && memberProp->simulatorDateStart.has_value()) {
                    seasonal::detail::MarkWindow(seasonal::detail::WindowOf(*memberProp), coverage);
                }
            }
            int coveredDays = 0;
            int overlapDays = 0;
            for (const uint8_t count : coverage) {
                coveredDays += count > 0 ? 1 : 0;
                overlapDays += count > 1 ? 1 : 0;
            }
            ImGui::Text("Year coverage: %d/365 days%s", coveredDays,
                        overlapDays > 0 ? "" : "  (no overlap)");
            if (overlapDays > 0) {
                ImGui::SameLine();
                ImGui::TextDisabled("(%d days double-covered)", overlapDays);
            }

            for (const auto& member : set.members) {
                const uint32_t memberId = member.value();
                ImGui::PushID(static_cast<int>(memberId));
                if (const Prop* memberProp = props_->FindPropByInstanceId(memberId)) {
                    ImGui::BulletText("%s  [%s]", PropDisplayName_(*memberProp).c_str(),
                                      DateWindowText_(*memberProp).c_str());
                }
                else {
                    ImGui::BulletText("0x%08X (missing)", memberId);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    pending = PendingAction{PendingAction::Kind::RemoveMember, setIndex, memberId, {}};
                }
                ImGui::PopID();
            }

            if (!set.members.empty()) {
                const auto suggestions = SuggestSeasonalSetMembers_(set);
                if (!suggestions.empty()) {
                    ImGui::Spacing();
                    ImGui::TextDisabled("Suggested members (same group, complementary dates):");
                    for (const Prop* suggestion : suggestions) {
                        const uint32_t suggestionId = suggestion->instanceId.value();
                        ImGui::PushID(static_cast<int>(suggestionId));
                        ImGui::BulletText("%s  [%s]", PropDisplayName_(*suggestion).c_str(),
                                          DateWindowText_(*suggestion).c_str());
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Add")) {
                            pending = PendingAction{PendingAction::Kind::AddMember, setIndex, suggestionId, {}};
                        }
                        ImGui::PopID();
                    }
                }
            }
        }
        ImGui::PopID();
    }

    if (pending.has_value()) {
        bool changed = false;
        switch (pending->kind) {
        case PendingAction::Kind::AddMember:
            changed = favorites_->AddPropToUserSeasonalSet(pending->propId, pending->setIndex);
            break;
        case PendingAction::Kind::RemoveMember:
            changed = favorites_->RemovePropFromUserSeasonalSet(pending->propId, pending->setIndex);
            break;
        case PendingAction::Kind::Rename:
            changed = favorites_->RenameUserSeasonalSet(pending->setIndex, pending->name);
            break;
        case PendingAction::Kind::DeleteSet:
            changed = favorites_->DeleteUserSeasonalSet(pending->setIndex);
            break;
        }
        if (changed) {
            ApplyUserSeasonalSetsToRepository_();
        }
    }

    ImGui::End();
}

bool PropPanelTab::HasPropTooltipContent_(const Prop& prop) const {
    const PackCatalogRepository* catalog = director_->GetPackCatalog();
    return (catalog && !catalog->ExemplarRows(prop.groupId.value(), prop.instanceId.value()).empty()) ||
        HasPropDetails_(prop);
}

bool PropPanelTab::HasPropDetails_(const Prop& prop) {
    return !prop.familyIds.empty() ||
        prop.nighttimeStateChange.has_value() ||
        prop.timeOfDay.has_value() ||
        prop.simulatorDateStart.has_value() ||
        prop.simulatorDateDuration.has_value() ||
        prop.simulatorDateInterval.has_value() ||
        prop.randomChance.has_value();
}

bool PropPanelTab::RenderPropPills_(const Prop& prop, const bool startOnNewLine) const {
    bool renderedAny = false;

    const auto renderInlinePill = [&](const char* label, const ImU32 baseColor, const ImU32 hoverColor) {
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

    const SeasonalSet* seasonalSet = props_ ? props_->FindSeasonalSetForProp(prop.instanceId.value()) : nullptr;
    Badges::ForEachBadge(prop, seasonalSet, renderInlinePill);

    return renderedAny;
}

void PropPanelTab::RenderPropTooltip_(const Prop& prop) const {
    ImGui::BeginTooltip();
    RenderPropTooltipBody_(prop);
    ImGui::EndTooltip();
}

void PropPanelTab::RenderPropTooltipBody_(const Prop& prop) const {
    const PackCatalogRepository* catalog = director_->GetPackCatalog();
    const auto rows = catalog ? catalog->ExemplarRows(prop.groupId.value(), prop.instanceId.value())
                              : std::span<const PackCatalogRepository::ExemplarRow>{};
    if (const TexturePack* pack = rows.empty() ? nullptr : catalog->Pack(static_cast<int32_t>(rows.front()[3]))) {
        // props.cbor does not say which file a prop is loaded from, so the first pack stands for all.
        ImGui::Text("From %s by %s", pack->name.c_str(), pack->author.c_str());
        if (const std::string* file = catalog->File(rows.front()[2])) {
            ImGui::TextDisabled("%s", file->c_str());
        }
        std::vector<uint32_t> others;
        for (const auto& row : rows) {
            if (row[3] != rows.front()[3] && std::ranges::find(others, row[3]) == others.end()) {
                others.push_back(row[3]);
            }
        }
        if (!others.empty()) {
            ImGui::TextDisabled("Also in %zu other pack%s", others.size(), others.size() == 1 ? "" : "s");
        }
        if (HasPropDetails_(prop)) {
            ImGui::Separator();
        }
    }

    if (!prop.familyIds.empty()) {
        const auto& familyNames = props_->GetPropFamilyNames();
        ImGui::Text("Member of %zu famil%s", prop.familyIds.size(), prop.familyIds.size() == 1 ? "y" : "ies");
        for (const auto& familyIdHex : prop.familyIds) {
            const uint32_t familyId = familyIdHex.value();
            const auto it = familyNames.find(familyId);
            if (it != familyNames.end()) {
                ImGui::BulletText("%s (0x%08X)", it->second.c_str(), familyId);
            }
            else {
                ImGui::BulletText("0x%08X", familyId);
            }
        }
    }

    const SeasonalSet* seasonalSet = props_ ? props_->FindSeasonalSetForProp(prop.instanceId.value()) : nullptr;

    const bool hasTimedData =
        prop.nighttimeStateChange.has_value() ||
        prop.timeOfDay.has_value() ||
        prop.simulatorDateStart.has_value() ||
        prop.simulatorDateDuration.has_value() ||
        prop.simulatorDateInterval.has_value();
    const bool hasBehaviorMetadata = hasTimedData || prop.randomChance.has_value();

    if (hasBehaviorMetadata) {
        if (!prop.familyIds.empty()) {
            ImGui::Separator();
        }

        ImGui::Text("Behavior: %s", Badges::BuildBehaviorSummary(prop, seasonalSet).c_str());
    }

    if (seasonalSet != nullptr) {
        const char* origin = seasonalSet->confidence == 2 ? "custom"
                           : seasonalSet->confidence == 1 ? "auto-detected, fuzzy"
                                                          : "auto-detected";
        ImGui::Text("Seasonal set: %s (%s)", seasonalSet->name.c_str(), origin);
        for (const auto& member : seasonalSet->members) {
            if (const Prop* memberProp = props_->FindPropByInstanceId(member.value())) {
                ImGui::BulletText("%s", PropDisplayName_(*memberProp).c_str());
            }
            else {
                ImGui::BulletText("0x%08X", member.value());
            }
        }
    }
    else if (hasTimedData && Badges::HasSeasonalTiming(prop)) {
        ImGui::TextDisabled("Has seasonal timing but is not linked to a set.");
    }

    if (hasTimedData) {
        if (prop.nighttimeStateChange.has_value()) {
            ImGui::BulletText("Has separate day and night states: %s",
                              *prop.nighttimeStateChange ? "Yes" : "No");
        }

        if (prop.timeOfDay.has_value()) {
            char startBuffer[6]{};
            char endBuffer[6]{};
            FormatHour_(prop.timeOfDay->startHour, startBuffer, sizeof(startBuffer));
            FormatHour_(prop.timeOfDay->endHour, endBuffer, sizeof(endBuffer));
            ImGui::BulletText("Visible between %s and %s", startBuffer, endBuffer);
        }

        if (prop.simulatorDateStart.has_value()) {
            ImGui::BulletText("Starts on %s %u",
                              MonthName_(prop.simulatorDateStart->month),
                              static_cast<unsigned>(prop.simulatorDateStart->day));
        }

        if (prop.simulatorDateDuration.has_value()) {
            ImGui::BulletText("Stays active for %u day%s",
                              *prop.simulatorDateDuration,
                              *prop.simulatorDateDuration == 1 ? "" : "s");
        }

        if (prop.simulatorDateInterval.has_value()) {
            ImGui::BulletText("Repeats every %u day%s",
                              *prop.simulatorDateInterval,
                              *prop.simulatorDateInterval == 1 ? "" : "s");
        }
    }

    if (prop.randomChance.has_value()) {
        if (!prop.familyIds.empty() || hasBehaviorMetadata) {
            ImGui::Separator();
        }

        ImGui::Text("Spawn chance: %u%%", *prop.randomChance);
    }
}

void PropPanelTab::RenderRotationModal_() {
    if (pendingPaint_.open) {
        director_->ApplyGridChoice(&PaintGridChoices::props, pendingPaint_.settings);
        ImGui::OpenPopup("Prop Painter");
        pendingPaint_.open = false;
    }

    if (ImGui::BeginPopupModal("Prop Painter", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Prop: %s", pendingPaint_.propName.c_str());
        ImGui::Separator();

        RenderPaintOptions_(pendingPaint_.settings, "props", true);

        if (const SeasonalSet* set = props_ ? props_->FindSeasonalSetForProp(pendingPaint_.propId) : nullptr) {
            ImGui::Separator();
            ImGui::Checkbox("Paint full seasonal set", &pendingPaint_.settings.paintSeasonalSets);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Place all %zu members of '%s' at the same location. The model then changes with the seasons.",
                                  set->members.size(), set->name.c_str());
            }
        }

        ImGui::Separator();
        if (RenderStartCancel_()) {
            ReleaseImGuiInputCapture_();
            director_->SaveGridChoice(&PaintGridChoices::props, pendingPaint_.settings);
            director_->StartPropPainting(pendingPaint_.propId, pendingPaint_.settings, pendingPaint_.propName);
        }

        ImGui::EndPopup();
    }
}

void PropPanelTab::HandlePickedProp_(const PickedProp& picked) {
    if (!props_) {
        return;
    }

    const Prop* prop = props_->FindPropByInstanceId(picked.propType);
    if (!prop) {
        LOG_WARN("Picked prop 0x{:08X}, but it is not present in the prop cache", picked.propType);
        return;
    }

    filterHelper_.ResetFilters();
    packFilter_.Clear();
    filterHelper_.searchBuffer = PropDisplayName_(*prop);
    filteredPropsDirty_ = true;

    pendingPaint_.propId = prop->instanceId.value();
    pendingPaint_.propName = PropDisplayName_(*prop);
    selectedPropKey_ = MakeGIKey(prop->groupId.value(), prop->instanceId.value());
    pendingPaint_.settings.mode = PaintMode::Direct;
    pendingPaint_.settings.rotation = picked.orientation & 3;
    pendingPaint_.open = true;
}
