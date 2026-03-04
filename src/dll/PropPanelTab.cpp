#include "PropPanelTab.hpp"

#include <cstdio>
#include <cstring>
#include "Constants.hpp"
#include "Utils.hpp"
#include "rfl/visit.hpp"
#include "utils/Logger.h"

namespace {
    constexpr ImU32 kFamilyPillColor = IM_COL32(48, 102, 96, 255);
    constexpr ImU32 kFamilyPillHoverColor = IM_COL32(60, 124, 118, 255);
    constexpr ImU32 kDayNightPillColor = IM_COL32(66, 88, 140, 255);
    constexpr ImU32 kDayNightPillHoverColor = IM_COL32(80, 104, 162, 255);
    constexpr ImU32 kTimedPillColor = IM_COL32(138, 98, 36, 255);
    constexpr ImU32 kTimedPillHoverColor = IM_COL32(160, 114, 46, 255);
    constexpr ImU32 kSeasonalPillColor = IM_COL32(70, 120, 62, 255);
    constexpr ImU32 kSeasonalPillHoverColor = IM_COL32(84, 142, 74, 255);
    constexpr ImU32 kChancePillColor = IM_COL32(128, 72, 44, 255);
    constexpr ImU32 kChancePillHoverColor = IM_COL32(150, 86, 54, 255);

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

    bool HasSeasonalTiming_(const Prop& prop) {
        return prop.simulatorDateStart.has_value() ||
            prop.simulatorDateDuration.has_value() ||
            prop.simulatorDateInterval.has_value();
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

    std::vector<PropView> propViews;
    propViews.reserve(props.size());
    for (const auto& prop : props) {
        propViews.push_back(PropView{&prop});
    }

    const auto filteredProps = filterHelper_.ApplyFiltersAndSort(
        propViews, favorites_->GetFavoritePropIds(), sortSpecs_);

    ImGui::Text("Showing %zu of %zu props", filteredProps.size(), propViews.size());
    if (director_->IsPropPainting()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop painting")) {
            director_->StopPropPainting();
        }
    }
    if (director_->IsPropStripping()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop stripping")) {
            director_->StopPropStripping();
        }
    }
    else {
        ImGui::SameLine();
        if (ImGui::SmallButton("Strip props")) {
            ReleaseImGuiInputCapture_();
            director_->StartPropStripping();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Click props in the city to remove them one by one.\nCtrl+Z to undo, ESC to stop.");
        }
    }

    // Table in scrollable child region so filters stay visible
    if (ImGui::BeginChild("PropTableRegion", ImVec2(0, 0), false)) {
        RenderTableInternal_(filteredProps, favorites_->GetFavoritePropIds());
    }
    ImGui::EndChild();

    RenderRotationModal_();
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

    const auto& propsById = props_->GetPropsById();
    if (!propsById.contains(propKey)) {
        LOG_WARN("Could not find prop with key 0x{:016X} in props map", propKey);
        return texture;
    }
    const Prop* prop = propsById.at(propKey);
    if (!prop) {
        LOG_WARN("Prop index entry for key 0x{:016X} is null", propKey);
        return texture;
    }
    if (!prop->thumbnail.has_value()) {
        LOG_WARN("Prop with key 0x{:016X} has no thumbnail", propKey);
        return texture;
    }

    const auto& thumbnail = prop->thumbnail.value();

    rfl::visit([&](const auto& variant) {
        const auto& data = variant.data;
        const uint32_t width = variant.width;
        const uint32_t height = variant.height;

        if (data.empty() || width == 0 || height == 0) {
            return;
        }

        const size_t expectedSize = static_cast<size_t>(width) * height * 4;
        if (data.size() != expectedSize) {
            LOG_WARN("Prop icon data size mismatch for key 0x{:016X}: expected {}, got {}",
                     propKey, expectedSize, data.size());
            return;
        }

        texture.Create(imguiService_, width, height, data.data());
    }, thumbnail);

    return texture;
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
    ImGui::SetNextItemWidth(UI::wideInputWidth());
    ImGui::SliderFloat2("##PropWidth", filterHelper_.propWidth, PropSize::kMinSize, PropSize::kMaxSize,
                        UI::kMeterFloatFormat);
    ImGui::SameLine();
    ImGui::Text("Height:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(UI::wideInputWidth());
    ImGui::SliderFloat2("##PropHeight", filterHelper_.propHeight, PropSize::kMinSize, PropSize::kMaxSize,
                        UI::kMeterFloatFormat);
    ImGui::SameLine();
    ImGui::Text("Depth:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(UI::wideInputWidth());
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
    const auto& props = props_->GetProps();
    std::vector<PropView> propViews;
    propViews.reserve(props.size());
    for (const auto& prop : props) {
        propViews.push_back(PropView{&prop});
    }

    const auto filteredProps = filterHelper_.ApplyFiltersAndSort(
        propViews, favorites_->GetFavoritePropIds(), sortSpecs_);

    RenderTableInternal_(filteredProps, favorites_->GetFavoritePropIds());
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
            if (!newSpecs.empty()) {
                sortSpecs_ = std::move(newSpecs);
                specs->SpecsDirty = false;
            }
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
                if (prop.thumbnail.has_value()) {
                    thumbnailCache_.Request(MakeGIKey(prop.groupId.value(), prop.instanceId.value()));
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
                if (thumbTextureId.has_value() && *thumbTextureId != nullptr) {
                    ImGui::Image(*thumbTextureId, ImVec2(UI::kIconSize, UI::kIconSize));
                }
                else {
                    ImGui::Dummy(ImVec2(UI::kIconSize, UI::kIconSize));
                }

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
                        HasSeasonalTiming_(prop) ||
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
                        switchedTarget = director_->SwitchPropPaintingTarget(prop.instanceId.value(), prop.visibleName);
                    }

                    if (switchedTarget) {
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
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(isFavorite ? "Remove from favorites" : "Add to favorites");
    }
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

std::string PropPanelTab::BuildBehaviorSummary_(const Prop& prop) const {
    const bool hasDayNight = prop.nighttimeStateChange.value_or(false);
    const bool hasTimeWindow = prop.timeOfDay.has_value();
    const bool hasSeasonalTiming = HasSeasonalTiming_(prop);

    if (hasDayNight && hasTimeWindow && hasSeasonalTiming) {
        return "Day/Night + Timed + Seasonal";
    }
    if (hasDayNight && hasTimeWindow) {
        return "Day/Night + Timed";
    }
    if (hasDayNight && hasSeasonalTiming) {
        return "Day/Night + Seasonal";
    }
    if (hasTimeWindow && hasSeasonalTiming) {
        return "Timed + Seasonal";
    }
    if (hasDayNight) {
        return "Day/Night";
    }
    if (hasTimeWindow) {
        return "Timed";
    }
    if (hasSeasonalTiming) {
        return "Seasonal";
    }

    return "Static";
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

        RenderPill_(label, baseColor, hoverColor);
        renderedAny = true;
        hoveredAny = hoveredAny || ImGui::IsItemHovered();
    };

    if (!prop.familyIds.empty()) {
        renderInlinePill("Family", kFamilyPillColor, kFamilyPillHoverColor);
    }
    if (prop.nighttimeStateChange.value_or(false)) {
        renderInlinePill("Day/Night", kDayNightPillColor, kDayNightPillHoverColor);
    }
    if (prop.timeOfDay.has_value()) {
        renderInlinePill("Timed", kTimedPillColor, kTimedPillHoverColor);
    }
    if (HasSeasonalTiming_(prop)) {
        renderInlinePill("Seasonal", kSeasonalPillColor, kSeasonalPillHoverColor);
    }
    if (prop.randomChance.has_value() && *prop.randomChance < 100) {
        char buffer[24]{};
        std::snprintf(buffer, sizeof(buffer), "%u%% Chance", *prop.randomChance);
        renderInlinePill(buffer, kChancePillColor, kChancePillHoverColor);
    }

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

        ImGui::Text("Behavior: %s", BuildBehaviorSummary_(prop).c_str());
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
        if (*prop.randomChance < 100) {
            ImGui::TextDisabled("The game may skip this prop on some placements.");
        }
    }

    ImGui::EndTooltip();
}

void PropPanelTab::RenderPill_(const char* label, const ImU32 baseColor, const ImU32 hoverColor) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, baseColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, hoverColor);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(235, 238, 242, 255));
    ImGui::SmallButton(label);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
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
        ImGui::RadioButton("Direct paint", &mode, static_cast<int>(PropPaintMode::Direct));
        ImGui::RadioButton("Paint along line", &mode, static_cast<int>(PropPaintMode::Line));
        ImGui::RadioButton("Paint inside polygon", &mode, static_cast<int>(PropPaintMode::Polygon));
        pendingPaint_.settings.mode = static_cast<PropPaintMode>(mode);

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
        ImGui::SliderFloat("Vertical offset (m)", &pendingPaint_.settings.deltaYMeters, 0.0f, 100.0f, "%.1f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Raises placed props above the terrain and preview grid.");
        }
        static constexpr const char* kPreviewModeLabels[] = {
            "Outline only",
            "Full prop only",
            "Outline + full prop"
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
            ImGui::TextWrapped("Click to add line points. Enter places props. Backspace removes the last point.");
        }
        else if (pendingPaint_.settings.mode == PropPaintMode::Polygon) {
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
