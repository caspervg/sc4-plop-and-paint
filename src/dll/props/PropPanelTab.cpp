#include "PropPanelTab.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include "../../shared/SeasonalSetDetector.hpp"
#include "../common/BadgeUtils.hpp"
#include "../common/Constants.hpp"
#include "../common/Utils.hpp"
#include "../utils/Logger.h"

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
        ImGui::TextUnformatted("No props loaded. Please ensure props.cbor exists in the Plugins directory.");
        return;
    }

    RenderFilterUI_();

    ImGui::Separator();
    EnsureFilteredPropsCache_();

    ImGui::Text("Showing %zu of %zu props", filteredPropsCache_.size(), allPropViewsCache_.size());
    if (director_->IsPropPainting()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop painting")) {
            director_->StopPropPainting();
        }
    }
    RenderPropStripperControls_();
    if (director_->IsPropPicking()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop picking##PropPicker")) {
            director_->StopPropPicking();
        }
    }
    else {
        ImGui::SameLine();
        const bool hasAnySource =
            (director_->GetPropStripperSources() &
                (PropStripperInputControl::SourceFlagCity |
                 PropStripperInputControl::SourceFlagLot |
                 PropStripperInputControl::SourceFlagStreet)) != 0;
        if (!hasAnySource) {
            ImGui::BeginDisabled();
        }
        if (ImGui::SmallButton("Pick prop")) {
            ReleaseImGuiInputCapture_();
            director_->StartPropPicking([this](const PickedProp& picked) {
                HandlePickedProp_(picked);
            });
        }
        if (!hasAnySource) {
            ImGui::EndDisabled();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Click an existing city, lot, or street prop to select it here.");
        }
    }

    // Table in scrollable child region so filters stay visible
    if (ImGui::BeginChild("PropTableRegion", ImVec2(0, 0), false)) {
        RenderTableInternal_(filteredPropsCache_, favorites_->GetFavoritePropIds());
    }
    ImGui::EndChild();

    RenderRotationModal_();
    RenderSeasonalSetEditor_();
}

void PropPanelTab::OnDeviceReset(const uint32_t deviceGeneration) {
    if (deviceGeneration != lastDeviceGeneration_) {
        thumbnailCache_.OnDeviceReset();
        lastDeviceGeneration_ = deviceGeneration;
    }
}

ImGuiTexture PropPanelTab::LoadPropTexture_(uint64_t propKey) {
    ImGuiTexture texture;

    if (!imguiService_) {
        LOG_WARN("Could not load prop thumbnail: imguiService_ is null");
        return texture;
    }

    auto data = props_->GetPropThumbnailStore().LoadThumbnail(propKey);
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

    filtersChanged = ImGui::Checkbox("Favorites only", &filterHelper_.favoritesOnly) || filtersChanged;
    ImGui::SameLine();
    filtersChanged = ImGui::Checkbox("In a family", &filterHelper_.requireFamily) || filtersChanged;
    ImGui::SameLine();
    filtersChanged = ImGui::Checkbox("Day/Night", &filterHelper_.requireDayNight) || filtersChanged;
    ImGui::SameLine();
    filtersChanged = ImGui::Checkbox("Timed", &filterHelper_.requireTimed) || filtersChanged;
    ImGui::SameLine();
    filtersChanged = ImGui::Checkbox("Seasonal", &filterHelper_.requireSeasonal) || filtersChanged;
    ImGui::SameLine();
    filtersChanged = ImGui::Checkbox("<100% Chance", &filterHelper_.requireReducedChance) || filtersChanged;

    ImGui::Text("Width:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(UI::wideInputWidth());
    filtersChanged = ImGui::SliderFloat2("##PropWidth", filterHelper_.propWidth, PropSize::kMinSize, PropSize::kMaxSize,
                                         UI::kMeterFloatFormat) || filtersChanged;
    ImGui::SameLine();
    ImGui::Text("Height:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(UI::wideInputWidth());
    filtersChanged = ImGui::SliderFloat2("##PropHeight", filterHelper_.propHeight, PropSize::kMinSize, PropSize::kMaxSize,
                                         UI::kMeterFloatFormat) || filtersChanged;
    ImGui::SameLine();
    ImGui::Text("Depth:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(UI::wideInputWidth());
    filtersChanged = ImGui::SliderFloat2("##PropDepth", filterHelper_.propDepth, PropSize::kMinSize, PropSize::kMaxSize,
                                         UI::kMeterFloatFormat) || filtersChanged;

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    if (ImGui::Button("Clear filters")) {
        filterHelper_.ResetFilters();
        filtersChanged = true;
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
    allPropViewsCache_.clear();
    allPropViewsCache_.reserve(props.size());
    for (const auto& prop : props) {
        allPropViewsCache_.push_back(PropView{&prop});
    }
    cachedPropCount_ = props.size();
    filteredPropsDirty_ = true;
}

void PropPanelTab::EnsureFilteredPropsCache_() {
    const auto& props = props_->GetProps();
    if (cachedPropCount_ != props.size() || allPropViewsCache_.size() != props.size()) {
        RebuildPropViewsCache_();
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
                ImGui::Selectable("##row", false,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                                  ImVec2(0, rowHeight));
                ImGui::SameLine();
                auto thumbTextureId = thumbnailCache_.Get(key);
                RenderThumbnail_(thumbTextureId);

                // Name
                ImGui::TableNextColumn();
                bool showPropTooltip = false;
                if (prop.visibleName.empty()) {
                    ImGui::TextUnformatted(prop.exemplarName.c_str());
                    showPropTooltip = ImGui::IsItemHovered();
                    if (RenderPropPills_(prop, true)) {
                        showPropTooltip = true;
                    }
                }
                else {
                    ImGui::TextUnformatted(prop.visibleName.c_str());
                    showPropTooltip = ImGui::IsItemHovered();
                    const bool hasInlinePills =
                        !prop.familyIds.empty() ||
                        prop.nighttimeStateChange.value_or(false) ||
                        prop.timeOfDay.has_value() ||
                        Badges::HasSeasonalTiming(prop) ||
                        (prop.randomChance.has_value() && *prop.randomChance < 100);
                    const bool hoveredPills = RenderPropPills_(prop, true);
                    if (hasInlinePills) {
                        ImGui::SameLine(0.0f, 4.0f);
                    }
                    ImGui::TextDisabled("%s", prop.exemplarName.c_str());
                    showPropTooltip = showPropTooltip || hoveredPills || ImGui::IsItemHovered();
                    if (hoveredPills) {
                        showPropTooltip = true;
                    }
                }

                if (showPropTooltip && HasPropTooltipContent_(prop)) {
                    RenderPropTooltip_(prop);
                }

                // Size
                ImGui::TableNextColumn();
                ImGui::Text("%.1f x %.1f x %.1f", prop.width, prop.height, prop.depth);

                // Actions
                ImGui::TableNextColumn();
                if (ImGui::Button("Paint")) {
                    bool switchedTarget = false;
                    if (director_->IsPropPainting()) {
                        ReleaseImGuiInputCapture_();
                        switchedTarget = director_->SwitchPropPaintingTarget(
                            prop.instanceId.value(),
                            PropDisplayName_(prop));
                    }

                    if (switchedTarget) {
                        // Reuse current paint mode and rotation; no modal.
                        }
                    else {
                        pendingPaint_.propId = prop.instanceId.value();
                        pendingPaint_.propName = PropDisplayName_(prop);
                        pendingPaint_.settings.mode = PaintMode::Direct;
                        pendingPaint_.settings.rotation = 0;
                        pendingPaint_.open = true;
                    }
                }
                ImGui::SameLine();
                RenderFavButton_(prop);
                ImGui::SameLine();
                if (ImGui::Button("Fam")) {
                    ImGui::OpenPopup("AddToPalette");
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Add to user-defined family");
                }

                if (ImGui::BeginPopup("AddToPalette")) {
                    const auto& userFamilies = favorites_->GetUserFamilies();
                    if (userFamilies.empty()) {
                        ImGui::TextDisabled("No families yet.");
                        ImGui::TextDisabled("Create a user-defined family in the Families tab first.");
                    }
                    else {
                        for (size_t familyIndex = 0; familyIndex < userFamilies.size(); ++familyIndex) {
                            if (ImGui::Selectable(userFamilies[familyIndex].name.c_str())) {
                                favorites_->AddPropToUserFamily(prop.instanceId.value(), familyIndex);
                                ImGui::CloseCurrentPopup();
                                break;
                            }
                        }
                    }
                    ImGui::EndPopup();
                }

                ImGui::SameLine();
                RenderSeasonalSetRowActions_(prop);

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

void PropPanelTab::RenderSeasonalSetRowActions_(const Prop& prop) {
    if (ImGui::Button("Set")) {
        ImGui::OpenPopup("AddToSeasonalSet");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Add to a custom seasonal set");
    }

    if (!ImGui::BeginPopup("AddToSeasonalSet")) {
        return;
    }

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

    ImGui::EndPopup();
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
    if (!ImGui::Begin("Seasonal sets", &seasonalSetEditorOpen_)) {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped("Custom seasonal sets are painted and stripped as one stack. "
                       "A custom set overrides any auto-detected set that shares a member.");
    ImGui::InputTextWithHint("##NewSetName", "New set name", newSeasonalSetName_, sizeof(newSeasonalSetName_));
    ImGui::SameLine();
    if (ImGui::Button("Create") && newSeasonalSetName_[0] != '\0') {
        if (favorites_->CreateUserSeasonalSet(newSeasonalSetName_)) {
            newSeasonalSetName_[0] = '\0';
            ApplyUserSeasonalSetsToRepository_();
        }
    }
    ImGui::Separator();

    const auto& userSets = favorites_->GetUserSeasonalSets();
    if (userSets.empty()) {
        ImGui::TextDisabled("No custom sets yet. Use the Set button in the prop table to start one.");
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
            ImGui::InputTextWithHint("##RenameSet", "New name", renameSeasonalSetName_,
                                     sizeof(renameSeasonalSetName_));
            ImGui::SameLine();
            if (ImGui::Button("Rename") && renameSeasonalSetName_[0] != '\0') {
                pending = PendingAction{PendingAction::Kind::Rename, setIndex, 0, renameSeasonalSetName_};
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete set")) {
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
            if (changed) {
                renameSeasonalSetName_[0] = '\0';
            }
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
    bool hoveredAny = false;

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
        hoveredAny = hoveredAny || ImGui::IsItemHovered();
    };

    const SeasonalSet* seasonalSet = props_ ? props_->FindSeasonalSetForProp(prop.instanceId.value()) : nullptr;
    Badges::ForEachBadge(prop, seasonalSet, renderInlinePill);

    return hoveredAny;
}

void PropPanelTab::RenderPropTooltip_(const Prop& prop) const {
    ImGui::BeginTooltip();

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

    ImGui::EndTooltip();
}

void PropPanelTab::RenderRotationModal_() {
    if (pendingPaint_.open) {
        ImGui::OpenPopup("Prop Painter");
        pendingPaint_.open = false;
    }

    if (ImGui::BeginPopupModal("Prop Painter", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Prop: %s", pendingPaint_.propName.c_str());
        ImGui::Separator();

        ImGui::TextUnformatted("Mode");
        int mode = static_cast<int>(pendingPaint_.settings.mode);
        ImGui::RadioButton("Direct paint", &mode, static_cast<int>(PaintMode::Direct));
        ImGui::RadioButton("Paint along line", &mode, static_cast<int>(PaintMode::Line));
        ImGui::RadioButton("Paint inside polygon", &mode, static_cast<int>(PaintMode::Polygon));
        pendingPaint_.settings.mode = static_cast<PaintMode>(mode);

        ImGui::Separator();
        ImGui::TextUnformatted("Rotation");
        int rotation = pendingPaint_.settings.rotation;
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
            bool snapPlacements = false;
            ImGui::Checkbox("Also snap placements to grid", &snapPlacements);
            ImGui::EndDisabled();
        }
        ImGui::SliderFloat("Grid step (m)", &pendingPaint_.settings.gridStepMeters, 1.0f, 16.0f, "%.1f");
        if (pendingPaint_.settings.mode == PaintMode::Line || pendingPaint_.settings.mode == PaintMode::Polygon) {
            bool customNodeHeights = pendingPaint_.settings.sketchHeightMode == SketchHeightMode::Custom;
            if (ImGui::Checkbox("Custom node heights", &customNodeHeights)) {
                pendingPaint_.settings.sketchHeightMode = customNodeHeights
                    ? SketchHeightMode::Custom
                    : SketchHeightMode::Terrain;
            }
        }
        ImGui::TextDisabled("Use [ ] +/-1.0m, Shift+[ ] +/-5.0m, Shift+Alt+[ ] +/-0.1m.");
        if (pendingPaint_.settings.mode == PaintMode::Direct) {
            ImGui::TextDisabled("Press H in-game to capture the absolute reference height.");
        }
        static constexpr const char* kPreviewModeLabels[] = {
            "Outline only",
            "Full prop only",
            "Outline + full prop"
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
            ImGui::TextWrapped("Click to add line points. Enter places props. Backspace removes the last point.");
        }
        else if (pendingPaint_.settings.mode == PaintMode::Polygon) {
            ImGui::Separator();
            ImGui::SliderFloat("Density (/100 m^2)", &pendingPaint_.settings.densityPer100Sqm, 0.1f, 10.0f, "%.2f");
            ImGui::SliderFloat("Density variation", &pendingPaint_.settings.densityVariation, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("0 = uniform fill, 1 = patchier clusters and gaps.");
            }
            ImGui::Checkbox("Random rotation", &pendingPaint_.settings.randomRotation);
            ImGui::TextWrapped("Click to add polygon vertices. Enter fills with props. Backspace removes the last vertex.");
        }
        else {
            ImGui::TextWrapped("Click to place props directly. Enter commits placed props.");
        }

        if (const SeasonalSet* set = props_ ? props_->FindSeasonalSetForProp(pendingPaint_.propId) : nullptr) {
            ImGui::Separator();
            ImGui::Checkbox("Paint full seasonal set", &pendingPaint_.settings.paintSeasonalSets);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Places all %zu members of '%s' at the same spot so the model follows the seasons.",
                                  set->members.size(), set->name.c_str());
            }
        }

        const bool canStart = true;

        if (ImGui::Button("Start") && canStart) {
            ReleaseImGuiInputCapture_();
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
    filterHelper_.searchBuffer = PropDisplayName_(*prop);
    filteredPropsDirty_ = true;

    pendingPaint_.propId = prop->instanceId.value();
    pendingPaint_.propName = PropDisplayName_(*prop);
    pendingPaint_.settings.mode = PaintMode::Direct;
    pendingPaint_.settings.rotation = picked.orientation & 3;
    pendingPaint_.open = true;
}
