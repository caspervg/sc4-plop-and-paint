#include "FamiliesPanelTab.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <optional>

#include "Utils.hpp"
#include "rfl/visit.hpp"

namespace {
    std::string ToUpperCopy(const std::string& value) {
        std::string normalized = value;
        std::ranges::transform(normalized, normalized.begin(), [](const unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        return normalized;
    }

    bool ContainsCaseInsensitive(const std::string& text, const std::string& needle) {
        if (needle.empty()) {
            return true;
        }

        return ToUpperCopy(text).contains(ToUpperCopy(needle));
    }

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
    return "Families";
}

void FamiliesPanelTab::OnRender() {
    if (!imguiService_) {
        ImGui::TextDisabled("ImGui service unavailable.");
        return;
    }

    const auto& autoFamilies = props_->GetAutoFamilies();
    const auto& autoFamilyIds = props_->GetAutoFamilyIds();
    const auto& userFamilies = favorites_->GetUserFamilies();
    const size_t autoCount = autoFamilies.size();
    const size_t totalCount = autoCount + userFamilies.size();

    // Clamp selected index
    if (totalCount == 0) {
        ImGui::TextDisabled("No families yet.");
        ImGui::TextWrapped("Built-in families appear after loading prop data. "
            "Create a user family here, then add props from the Props tab.");
        if (ImGui::Button("+")) {
            newFamilyPopupOpen_ = true;
            std::strncpy(newFamilyName_, "New family", sizeof(newFamilyName_) - 1);
            newFamilyName_[sizeof(newFamilyName_) - 1] = '\0';
        }
        RenderNewFamilyPopup_();
        return;
    }

    if (selectedFamilyIndex_ >= totalCount) {
        selectedFamilyIndex_ = totalCount - 1;
    }

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
    ImGui::SameLine();
    if (ImGui::Button("Clear filters")) {
        nameFilter_.clear();
        iidFilter_.clear();
        nameFilterBuf[0] = '\0';
        iidFilterBuf[0] = '\0';
    }

    ImGui::Separator();

    struct FamilyListRow {
        size_t combinedIndex = 0;
        bool isAuto = false;
        const PropFamily* family = nullptr;
        std::optional<uint32_t> familyId{};
    };

    std::vector<FamilyListRow> filteredFamilies;
    filteredFamilies.reserve(totalCount);
    for (size_t i = 0; i < totalCount; ++i) {
        const bool rowIsAuto = i < autoCount;
        const PropFamily* family = rowIsAuto ? &autoFamilies[i] : &userFamilies[i - autoCount];
        const std::optional<uint32_t> familyId = rowIsAuto && i < autoFamilyIds.size()
            ? std::optional<uint32_t>(autoFamilyIds[i])
            : std::nullopt;

        if (!ContainsCaseInsensitive(family->name, nameFilter_)) {
            continue;
        }
        if (!MatchesIidFilter(familyId, iidFilter_)) {
            continue;
        }

        filteredFamilies.push_back(FamilyListRow{
            .combinedIndex = i,
            .isAuto = rowIsAuto,
            .family = family,
            .familyId = familyId
        });
    }

    if (!filteredFamilies.empty() &&
        std::ranges::none_of(filteredFamilies, [this](const FamilyListRow& row) {
            return row.combinedIndex == selectedFamilyIndex_;
        })) {
        selectedFamilyIndex_ = filteredFamilies.front().combinedIndex;
    }

    ImGui::Text("Showing %zu of %zu families", filteredFamilies.size(), totalCount);
    if (director_->IsPropPainting()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop painting")) {
            director_->StopPropPainting();
        }
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("New family")) {
        newFamilyPopupOpen_ = true;
        std::strncpy(newFamilyName_, "New family", sizeof(newFamilyName_) - 1);
        newFamilyName_[sizeof(newFamilyName_) - 1] = '\0';
    }

    ImGui::Separator();

    constexpr ImGuiTableFlags familyTableFlags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBody |
        ImGuiTableFlags_ScrollY;

    if (ImGui::BeginChild("FamilyTableRegion", ImVec2(0, UI::familyTableHeight()), false)) {
        if (ImGui::BeginTable("FamiliesTable", 5, familyTableFlags, ImVec2(0, 0))) {
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, UI::typeColumnWidth());
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Instance ID", ImGuiTableColumnFlags_WidthFixed, UI::instanceIdColumnWidth());
            ImGui::TableSetupColumn("Props", ImGuiTableColumnFlags_WidthFixed, UI::propsColumnWidth());
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, UI::familyActionColWidth());
            ImGui::TableHeadersRow();

            for (const auto& row : filteredFamilies) {
                const bool selected = row.combinedIndex == selectedFamilyIndex_;

                ImGui::PushID(static_cast<int>(row.combinedIndex));
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Selectable("##familyrow", selected,
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
                const std::string familyIdLabel = FormatFamilyId(row.familyId);
                ImGui::TextUnformatted(familyIdLabel.c_str());

                ImGui::TableNextColumn();
                ImGui::Text("%zu", row.family->entries.size());

                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Paint")) {
                    selectedFamilyIndex_ = row.combinedIndex;
                    QueuePaintForSelectedFamily_();
                }

                ImGui::PopID();
            }

            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    if (filteredFamilies.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("No families match the current filters.");
        RenderNewFamilyPopup_();
        RenderPaintOptionsPopup_();
        return;
    }

    const PropFamily* selectedFamily = GetSelectedFamily_();
    const bool selectedIsAuto = IsSelectedAutoFamily_();
    const size_t userIndex = selectedIsAuto ? 0 : selectedFamilyIndex_ - autoCount;

    if (!selectedFamily) {
        RenderNewFamilyPopup_();
        return;
    }

    if (selectedIsAuto) {
        ImGui::TextDisabled("Read-only built-in family");
    }
    else if (ImGui::SmallButton("Delete family")) {
        deleteFamilyPopupOpen_ = true;
    }
    ImGui::TextUnformatted(selectedFamily->name.c_str());

    ImGui::Separator();
    if (selectedIsAuto && selectedFamilyIndex_ < autoFamilyIds.size()) {
        ImGui::Text("IID: 0x%08X", autoFamilyIds[selectedFamilyIndex_]);
    }
    else if (!selectedIsAuto) {
        ImGui::TextDisabled("IID: custom family");
    }
    ImGui::Text("%zu props in family", selectedFamily->entries.size());

    if (selectedFamily->entries.empty()) {
        if (selectedIsAuto) {
            ImGui::TextDisabled("Empty auto family.");
        }
        else {
            ImGui::TextDisabled("Empty family. Use 'Fam' in the Props tab to add entries.");
        }
    }
    else {
        if (ImGui::BeginTable("FamilyEntries", selectedIsAuto ? 3 : 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                              ImVec2(0, UI::familyEntriesHeight()))) {
            ImGui::TableSetupColumn("##icon", ImGuiTableColumnFlags_WidthFixed, UI::iconColumnWidth());
            ImGui::TableSetupColumn("Prop", ImGuiTableColumnFlags_WidthStretch);
            if (!selectedIsAuto) {
                ImGui::TableSetupColumn("Weight", ImGuiTableColumnFlags_WidthFixed, UI::weightColumnWidth());
            }
            ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, selectedIsAuto ? 0.0f : UI::removeColumnWidth());
            ImGui::TableHeadersRow();

            // We need a mutable reference for weight editing / removal (user families only)
            int removeIndex = -1;

            auto renderEntry = [&](const size_t i, const FamilyEntry& constEntry) {
                const Prop* prop = props_->FindPropByInstanceId(constEntry.propID.value());

                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow(0, UI::iconRowHeight());

                ImGui::TableNextColumn();
                if (prop && prop->thumbnail) {
                    const uint64_t key = MakeGIKey(prop->groupId.value(), prop->instanceId.value());
                    thumbnailCache_.Request(key);
                    auto thumbTextureId = thumbnailCache_.Get(key);
                    if (thumbTextureId.has_value() && *thumbTextureId != nullptr) {
                        ImGui::Image(*thumbTextureId, ImVec2(UI::kIconSize, UI::kIconSize));
                    }
                    else {
                        ImGui::Dummy(ImVec2(UI::kIconSize, UI::kIconSize));
                    }
                }
                else {
                    ImGui::Dummy(ImVec2(UI::kIconSize, UI::kIconSize));
                }

                ImGui::TableNextColumn();
                if (prop) {
                    ImGui::TextUnformatted(PropDisplayName_(*prop).c_str());
                }
                else {
                    ImGui::Text("Missing 0x%08X", constEntry.propID.value());
                }

                if (!selectedIsAuto) {
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-1.0f);
                    auto& [propID, weight] = favorites_->GetUserFamilies()[userIndex].entries[i];
                    if (ImGui::SliderFloat("##weight", &weight, 0.1f, 10.0f, "%.1f")) {
                        favorites_->Save();
                    }

                    ImGui::TableNextColumn();
                    if (ImGui::SmallButton("x")) {
                        removeIndex = static_cast<int>(i);
                    }
                }

                ImGui::PopID();
            };

            for (size_t i = 0; i < selectedFamily->entries.size(); ++i) {
                renderEntry(i, selectedFamily->entries[i]);
            }

            ImGui::EndTable();

            if (!selectedIsAuto && removeIndex >= 0) {
                favorites_->GetUserFamilies()[userIndex].entries.erase(
                    favorites_->GetUserFamilies()[userIndex].entries.begin() + removeIndex);
                favorites_->Save();
            }
        }

        thumbnailCache_.ProcessLoadQueue([this](const uint64_t key) {
            return LoadPropTexture_(key);
        });
    }

    // --- Density variation (user families only) ---
    if (!selectedIsAuto) {
        ImGui::Separator();
        ImGui::TextUnformatted("Family Settings");
        auto& mutableFamily = favorites_->GetUserFamilies()[userIndex];
        if (ImGui::SliderFloat("Density variation", &mutableFamily.densityVariation, 0.0f, 1.0f, "%.2f")) {
            favorites_->Save();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0 = uniform spacing, 1 = patchier distribution.");
        }
    }

    // --- Paint button ---
    ImGui::Separator();
    if (selectedFamily->entries.empty()) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Paint family")) {
        QueuePaintForSelectedFamily_();
    }
    if (selectedFamily->entries.empty()) {
        ImGui::EndDisabled();
    }

    RenderNewFamilyPopup_();
    if (!selectedIsAuto) {
        RenderDeleteFamilyPopup_(userIndex);
    }
    RenderPaintOptionsPopup_();
}

void FamiliesPanelTab::OnDeviceReset(const uint32_t deviceGeneration) {
    if (deviceGeneration != lastDeviceGeneration_) {
        thumbnailCache_.OnDeviceReset();
        lastDeviceGeneration_ = deviceGeneration;
    }
}

ImGuiTexture FamiliesPanelTab::LoadPropTexture_(const uint64_t propKey) {
    ImGuiTexture texture;

    if (!imguiService_) {
        return texture;
    }

    const auto& propsById = props_->GetPropsById();
    if (!propsById.contains(propKey)) {
        return texture;
    }

    const Prop* prop = propsById.at(propKey);
    if (!prop || !prop->thumbnail.has_value()) {
        return texture;
    }

    rfl::visit([&](const auto& variant) {
        const auto& data = variant.data;
        const uint32_t width = variant.width;
        const uint32_t height = variant.height;

        if (data.empty() || width == 0 || height == 0) {
            return;
        }

        const size_t expectedSize = static_cast<size_t>(width) * height * 4;
        if (data.size() != expectedSize) {
            return;
        }

        texture.Create(imguiService_, width, height, data.data());
    }, prop->thumbnail.value());

    return texture;
}

void FamiliesPanelTab::RenderNewFamilyPopup_() {
    if (!newFamilyPopupOpen_) {
        return;
    }

    ImGui::OpenPopup("Create Custom Family");
    if (ImGui::BeginPopupModal("Create Custom Family", &newFamilyPopupOpen_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Name", newFamilyName_, sizeof(newFamilyName_));

        const bool canCreate = std::strlen(newFamilyName_) > 0;
        if (!canCreate) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Create")) {
            if (favorites_->CreateUserFamily(newFamilyName_)) {
                // Select the newly created user family
                selectedFamilyIndex_ = props_->GetAutoFamilies().size() + favorites_->GetUserFamilies().size() - 1;
            }
            newFamilyPopupOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        if (!canCreate) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            newFamilyPopupOpen_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void FamiliesPanelTab::RenderDeleteFamilyPopup_(const size_t userFamilyIndex) {
    if (!deleteFamilyPopupOpen_) {
        return;
    }

    ImGui::OpenPopup("Delete Family");
    if (ImGui::BeginPopupModal("Delete Family", &deleteFamilyPopupOpen_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Delete this family?");

        if (ImGui::Button("Delete")) {
            favorites_->DeleteUserFamily(userFamilyIndex);
            const size_t total = props_->GetAutoFamilies().size() + favorites_->GetUserFamilies().size();
            if (selectedFamilyIndex_ >= total && total > 0) {
                selectedFamilyIndex_ = total - 1;
            }
            deleteFamilyPopupOpen_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            deleteFamilyPopupOpen_ = false;
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

    pendingPaint_.fallbackPropId = family->entries.front().propID.value();
    pendingPaint_.familyName = family->name;
    pendingPaint_.settings = familyPaintDefaults_;
    pendingPaint_.settings.activePalette = family->entries;
    pendingPaint_.settings.densityVariation = family->densityVariation;
    pendingPaint_.settings.randomSeed = 0;
    pendingPaint_.open = true;
}

void FamiliesPanelTab::RenderPaintOptionsPopup_() {
    if (pendingPaint_.open) {
        ImGui::OpenPopup("Family Paint Options");
        pendingPaint_.open = false;
    }

    if (ImGui::BeginPopupModal("Family Paint Options", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Family: %s", pendingPaint_.familyName.c_str());
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
            ImGui::SliderFloat("Density (/100 m^2)", &pendingPaint_.settings.densityPer100Sqm, 0.1f, 20.0f, "%.1f");
            ImGui::Checkbox("Random rotation", &pendingPaint_.settings.randomRotation);
            ImGui::TextWrapped("Click to add polygon vertices. Enter fills with props. Backspace removes the last vertex.");
        }
        else {
            ImGui::TextWrapped("Click to place props directly. Enter commits placed props.");
        }

        if (ImGui::Button("Start")) {
            StartPaintingWithSelectedFamily_();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            familyPaintDefaults_ = pendingPaint_.settings;
            familyPaintDefaults_.activePalette.clear();
            familyPaintDefaults_.densityVariation = 0.0f;
            familyPaintDefaults_.randomSeed = 0;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

bool FamiliesPanelTab::StartPaintingWithSelectedFamily_() {
    if (pendingPaint_.fallbackPropId == 0 || pendingPaint_.settings.activePalette.empty()) {
        return false;
    }

    ReleaseImGuiInputCapture_();

    if (director_->IsPropPainting()) {
        director_->StopPropPainting();
    }

    PropPaintSettings settings = pendingPaint_.settings;
    if (settings.randomSeed == 0) {
        settings.randomSeed = static_cast<uint32_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
    }

    familyPaintDefaults_ = pendingPaint_.settings;
    familyPaintDefaults_.activePalette.clear();
    familyPaintDefaults_.densityVariation = 0.0f;
    familyPaintDefaults_.randomSeed = 0;

    return director_->StartPropPainting(pendingPaint_.fallbackPropId, settings, pendingPaint_.familyName);
}

std::string FamiliesPanelTab::PropDisplayName_(const Prop& prop) {
    if (!prop.visibleName.empty()) {
        return prop.visibleName;
    }
    if (!prop.exemplarName.empty()) {
        return prop.exemplarName;
    }
    return "<unnamed prop>";
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
