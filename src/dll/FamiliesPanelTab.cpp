#include "FamiliesPanelTab.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

#include "Utils.hpp"
#include "rfl/visit.hpp"
#include "spdlog/spdlog.h"

const char* FamiliesPanelTab::GetTabName() const {
    return "Families";
}

void FamiliesPanelTab::OnRender() {
    if (!imguiService_) {
        ImGui::TextDisabled("ImGui service unavailable.");
        return;
    }

    const auto& autoFamilies = props_->GetAutoFamilies();
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

    ImGui::TextDisabled("Built-in: %zu   Own: %zu", autoCount, userFamilies.size());
    const PropFamily* selectedFamily = GetSelectedFamily_();
    const bool isAuto = IsSelectedAutoFamily_();

    if (ImGui::BeginTable("FamilySelectors", 2)) {
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Built-in Families");
        if (autoFamilies.empty()) {
            ImGui::TextDisabled("No built-in families loaded.");
        }
        else {
            const std::string autoPreview = isAuto && selectedFamily ? selectedFamily->name : "(none)";
            if (ImGui::BeginCombo("##AutoFamilies", autoPreview.c_str())) {
                for (size_t i = 0; i < autoCount; ++i) {
                    const bool selected = i == selectedFamilyIndex_;
                    if (ImGui::Selectable(autoFamilies[i].name.c_str(), selected)) {
                        selectedFamilyIndex_ = i;
                    }
                }
                ImGui::EndCombo();
            }
        }

        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Own Families");
        if (userFamilies.empty()) {
            ImGui::TextDisabled("No own families yet.");
        }
        else {
            const std::string userPreview = !isAuto && selectedFamily ? selectedFamily->name : "(none)";
            if (ImGui::BeginCombo("##UserFamilies", userPreview.c_str())) {
                for (size_t i = 0; i < userFamilies.size(); ++i) {
                    const bool selected = (autoCount + i) == selectedFamilyIndex_;
                    if (ImGui::Selectable(userFamilies[i].name.c_str(), selected)) {
                        selectedFamilyIndex_ = autoCount + i;
                    }
                }
                ImGui::EndCombo();
            }
        }
        if (ImGui::Button("+")) {
            newFamilyPopupOpen_ = true;
            std::strncpy(newFamilyName_, "New family", sizeof(newFamilyName_) - 1);
            newFamilyName_[sizeof(newFamilyName_) - 1] = '\0';
        }

        if (!IsSelectedAutoFamily_()) {
            ImGui::SameLine();
            if (ImGui::Button("x")) {
                deleteFamilyPopupOpen_ = true;
            }
        }

        ImGui::EndTable();
    }

    selectedFamily = GetSelectedFamily_();
    const bool selectedIsAuto = IsSelectedAutoFamily_();
    const size_t userIndex = selectedIsAuto ? 0 : selectedFamilyIndex_ - autoCount;

    if (!selectedFamily) {
        RenderNewFamilyPopup_();
        return;
    }

    if (selectedIsAuto) {
        ImGui::TextDisabled("Read-only built-in family");
    }
    ImGui::TextUnformatted(selectedFamily->name.c_str());

    ImGui::Separator();
    ImGui::Text("%zu props in family", selectedFamily->entries.size());

    if (selectedFamily->entries.empty()) {
        if (selectedIsAuto) {
            ImGui::TextDisabled("Empty auto family.");
        }
        else {
            ImGui::TextDisabled("Empty family. Use '+' in the Props tab to add entries.");
        }
    }
    else {
        if (ImGui::BeginTable("FamilyEntries", selectedIsAuto ? 3 : 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                              ImVec2(0, 250))) {
            ImGui::TableSetupColumn("##icon", ImGuiTableColumnFlags_WidthFixed, 26.0f);
            ImGui::TableSetupColumn("Prop", ImGuiTableColumnFlags_WidthStretch);
            if (!selectedIsAuto) {
                ImGui::TableSetupColumn("Weight", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            }
            ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, selectedIsAuto ? 0.0f : 24.0f);
            ImGui::TableHeadersRow();

            // We need a mutable reference for weight editing / removal (user families only)
            int removeIndex = -1;

            auto renderEntry = [&](const size_t i, const FamilyEntry& constEntry) {
                const Prop* prop = props_->FindPropByInstanceId(constEntry.propID.value());

                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                if (prop && prop->thumbnail) {
                    const uint64_t key = MakeGIKey(prop->groupId.value(), prop->instanceId.value());
                    thumbnailCache_.Request(key);
                    auto thumbTextureId = thumbnailCache_.Get(key);
                    if (thumbTextureId.has_value() && *thumbTextureId != nullptr) {
                        ImGui::Image(*thumbTextureId, ImVec2(20, 20));
                    }
                    else {
                        ImGui::Dummy(ImVec2(20, 20));
                    }
                }
                else {
                    ImGui::Dummy(ImVec2(20, 20));
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
        auto& mutableFamily = favorites_->GetUserFamilies()[userIndex];
        if (ImGui::SliderFloat("Density variation", &mutableFamily.densityVariation, 0.0f, 1.0f, "%.2f")) {
            favorites_->Save();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0 = uniform spacing, 1 = patchier distribution.");
        }
    }

    // --- Paint defaults and paint buttons ---
    ImGui::Separator();
    ImGui::TextUnformatted("Paint Defaults");
    ImGui::TextUnformatted("Rotation");
    int rotation = familyPaintDefaults_.rotation;
    ImGui::RadioButton("0 deg", &rotation, 0);
    ImGui::SameLine();
    ImGui::RadioButton("90 deg", &rotation, 1);
    ImGui::SameLine();
    ImGui::RadioButton("180 deg", &rotation, 2);
    ImGui::SameLine();
    ImGui::RadioButton("270 deg", &rotation, 3);
    familyPaintDefaults_.rotation = rotation;

    ImGui::Separator();
    ImGui::TextUnformatted("Paint along line");
    ImGui::SliderFloat("Spacing (m)", &familyPaintDefaults_.spacingMeters, 0.5f, 50.0f, "%.1f");
    ImGui::Checkbox("Align to path direction", &familyPaintDefaults_.alignToPath);
    ImGui::SliderFloat("Lateral jitter (m)", &familyPaintDefaults_.randomOffset, 0.0f, 5.0f, "%.1f");

    ImGui::Separator();
    ImGui::TextUnformatted("Paint inside polygon");
    ImGui::SliderFloat("Density (/100 m^2)", &familyPaintDefaults_.densityPer100Sqm, 0.1f, 20.0f, "%.1f");
    ImGui::Checkbox("Random rotation", &familyPaintDefaults_.randomRotation);

    if (selectedFamily->entries.empty()) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Start line")) {
        StartPaintingWithSelectedFamily_(PropPaintMode::Line);
    }
    ImGui::SameLine();
    if (ImGui::Button("Start polygon")) {
        StartPaintingWithSelectedFamily_(PropPaintMode::Polygon);
    }
    if (selectedFamily->entries.empty()) {
        ImGui::EndDisabled();
    }

    RenderNewFamilyPopup_();
    if (!selectedIsAuto) {
        RenderDeleteFamilyPopup_(userIndex);
    }
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

    const auto& prop = propsById.at(propKey);
    if (!prop.thumbnail.has_value()) {
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
    }, prop.thumbnail.value());

    return texture;
}

void FamiliesPanelTab::RenderNewFamilyPopup_() {
    if (!newFamilyPopupOpen_) {
        return;
    }

    ImGui::OpenPopup("Create Family");
    if (ImGui::BeginPopupModal("Create Family", &newFamilyPopupOpen_, ImGuiWindowFlags_AlwaysAutoResize)) {
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

bool FamiliesPanelTab::StartPaintingWithSelectedFamily_(const PropPaintMode mode) {
    const PropFamily* family = GetSelectedFamily_();
    if (!family || family->entries.empty()) {
        return false;
    }

    if (director_->IsPropPainting()) {
        director_->StopPropPainting();
    }

    PropPaintSettings settings = familyPaintDefaults_;
    settings.mode = mode;
    settings.activePalette = family->entries;
    settings.densityVariation = family->densityVariation;

    if (settings.randomSeed == 0) {
        settings.randomSeed = static_cast<uint32_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
    }

    const uint32_t fallbackPropID = family->entries.front().propID.value();
    return director_->StartPropPainting(fallbackPropID, settings, family->name);
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
