#include "PalettesPanelTab.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

#include "Utils.hpp"
#include "rfl/visit.hpp"
#include "spdlog/spdlog.h"

const char* PalettesPanelTab::GetTabName() const {
    return "Palettes";
}

void PalettesPanelTab::OnRender() {
    if (!imguiService_) {
        ImGui::TextDisabled("ImGui service unavailable.");
        return;
    }

    auto& palettes = director_->GetPropPalettes();
    if (palettes.empty()) {
        ImGui::TextDisabled("No palettes yet.");
        ImGui::TextWrapped("Create a palette, then add props from the Props tab using the '+' button.");
        if (ImGui::Button("Create first palette")) {
            newPalettePopupOpen_ = true;
            std::strncpy(newPaletteName_, "New palette", sizeof(newPaletteName_) - 1);
            newPaletteName_[sizeof(newPaletteName_) - 1] = '\0';
        }
        RenderNewPalettePopup_();
        return;
    }

    size_t activeIndex = director_->GetActivePropPaletteIndex();
    if (activeIndex >= palettes.size()) {
        activeIndex = 0;
        director_->SetActivePropPaletteIndex(activeIndex);
    }

    if (ImGui::BeginCombo("Palette", palettes[activeIndex].name.c_str())) {
        for (size_t i = 0; i < palettes.size(); ++i) {
            const bool selected = i == activeIndex;
            if (ImGui::Selectable(palettes[i].name.c_str(), selected)) {
                director_->SetActivePropPaletteIndex(i);
                activeIndex = i;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+##newpalette")) {
        newPalettePopupOpen_ = true;
        std::strncpy(newPaletteName_, "New palette", sizeof(newPaletteName_) - 1);
        newPaletteName_[sizeof(newPaletteName_) - 1] = '\0';
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Create new palette");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("X##deletepalette")) {
        deletePalettePopupOpen_ = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Delete active palette");
    }

    auto& activePalette = palettes[activeIndex];
    char nameBuffer[128] = {};
    std::strncpy(nameBuffer, activePalette.name.c_str(), sizeof(nameBuffer) - 1);
    nameBuffer[sizeof(nameBuffer) - 1] = '\0';
    if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
        director_->RenamePropPalette(activeIndex, nameBuffer);
    }

    ImGui::Separator();
    ImGui::Text("%zu props in palette", activePalette.entries.size());

    if (activePalette.entries.empty()) {
        ImGui::TextDisabled("Empty palette. Use '+' in the Props tab to add entries.");
    }
    else {
        if (ImGui::BeginTable("PaletteEntries", 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                              ImVec2(0, 250))) {
            ImGui::TableSetupColumn("##icon", ImGuiTableColumnFlags_WidthFixed, 26.0f);
            ImGui::TableSetupColumn("Prop", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Weight", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, 24.0f);
            ImGui::TableHeadersRow();

            int removeIndex = -1;
            for (size_t i = 0; i < activePalette.entries.size(); ++i) {
                auto& entry = activePalette.entries[i];
                const Prop* prop = FindPropByInstanceID_(entry.propID.value());

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
                    ImGui::Text("Missing 0x%08X", entry.propID.value());
                }

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##weight", &entry.weight, 0.1f, 10.0f, "%.1f")) {
                    director_->SaveFavoritesNow();
                }

                ImGui::TableNextColumn();
                if (ImGui::SmallButton("X")) {
                    removeIndex = static_cast<int>(i);
                }

                ImGui::PopID();
            }

            ImGui::EndTable();

            if (removeIndex >= 0) {
                activePalette.entries.erase(activePalette.entries.begin() + removeIndex);
                director_->SaveFavoritesNow();
            }
        }

        thumbnailCache_.ProcessLoadQueue([this](const uint64_t key) {
            return LoadPropTexture_(key);
        });
    }

    ImGui::Separator();
    if (ImGui::SliderFloat("Density variation", &activePalette.densityVariation, 0.0f, 1.0f, "%.2f")) {
        director_->SaveFavoritesNow();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("0 = uniform spacing, 1 = patchier distribution (reserved for future placement tuning).");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Paint Defaults");
    ImGui::SliderFloat("Line spacing (m)", &palettePaintDefaults_.spacingMeters, 0.5f, 50.0f, "%.1f");
    ImGui::SliderFloat("Polygon density (/100 m^2)", &palettePaintDefaults_.densityPer100Sqm, 0.1f, 20.0f, "%.1f");
    ImGui::Checkbox("Align to path", &palettePaintDefaults_.alignToPath);
    ImGui::Checkbox("Random rotation", &palettePaintDefaults_.randomRotation);
    ImGui::SliderFloat("Lateral jitter (m)", &palettePaintDefaults_.randomOffset, 0.0f, 5.0f, "%.1f");
    int rotation = palettePaintDefaults_.rotation;
    ImGui::RadioButton("0 deg", &rotation, 0);
    ImGui::SameLine();
    ImGui::RadioButton("90 deg", &rotation, 1);
    ImGui::SameLine();
    ImGui::RadioButton("180 deg", &rotation, 2);
    ImGui::SameLine();
    ImGui::RadioButton("270 deg", &rotation, 3);
    palettePaintDefaults_.rotation = rotation;

    if (activePalette.entries.empty()) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Paint line")) {
        StartPaintingWithActivePalette_(PropPaintMode::Line);
    }
    ImGui::SameLine();
    if (ImGui::Button("Paint polygon")) {
        StartPaintingWithActivePalette_(PropPaintMode::Polygon);
    }
    if (activePalette.entries.empty()) {
        ImGui::EndDisabled();
    }

    RenderNewPalettePopup_();
    RenderDeletePalettePopup_(activeIndex);
}

void PalettesPanelTab::OnDeviceReset(const uint32_t deviceGeneration) {
    if (deviceGeneration != lastDeviceGeneration_) {
        thumbnailCache_.OnDeviceReset();
        lastDeviceGeneration_ = deviceGeneration;
    }
}

ImGuiTexture PalettesPanelTab::LoadPropTexture_(const uint64_t propKey) {
    ImGuiTexture texture;

    if (!imguiService_) {
        return texture;
    }

    const auto& propsById = director_->GetPropsById();
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

const Prop* PalettesPanelTab::FindPropByInstanceID_(const uint32_t propID) const {
    const auto& props = director_->GetProps();
    for (const auto& prop : props) {
        if (prop.instanceId.value() == propID) {
            return &prop;
        }
    }
    return nullptr;
}

void PalettesPanelTab::RenderNewPalettePopup_() {
    if (!newPalettePopupOpen_) {
        return;
    }

    ImGui::OpenPopup("Create Palette");
    if (ImGui::BeginPopupModal("Create Palette", &newPalettePopupOpen_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Name", newPaletteName_, sizeof(newPaletteName_));

        const bool canCreate = std::strlen(newPaletteName_) > 0;
        if (!canCreate) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Create")) {
            director_->CreatePropPalette(newPaletteName_);
            newPalettePopupOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        if (!canCreate) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            newPalettePopupOpen_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void PalettesPanelTab::RenderDeletePalettePopup_(const size_t index) {
    if (!deletePalettePopupOpen_) {
        return;
    }

    ImGui::OpenPopup("Delete Palette");
    if (ImGui::BeginPopupModal("Delete Palette", &deletePalettePopupOpen_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Delete this palette?");

        if (ImGui::Button("Delete")) {
            director_->DeletePropPalette(index);
            deletePalettePopupOpen_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            deletePalettePopupOpen_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

bool PalettesPanelTab::StartPaintingWithActivePalette_(const PropPaintMode mode) {
    const PropPalette* palette = director_->GetActivePropPalette();
    if (!palette || palette->entries.empty()) {
        return false;
    }

    PropPaintSettings settings = palettePaintDefaults_;
    settings.mode = mode;
    settings.activePalette = palette->entries;
    settings.densityVariation = palette->densityVariation;

    if (settings.randomSeed == 0) {
        settings.randomSeed = static_cast<uint32_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
    }

    const uint32_t fallbackPropID = palette->entries.front().propID.value();
    return director_->StartPropPainting(fallbackPropID, settings, palette->name);
}

std::string PalettesPanelTab::PropDisplayName_(const Prop& prop) {
    if (!prop.visibleName.empty()) {
        return prop.visibleName;
    }
    if (!prop.exemplarName.empty()) {
        return prop.exemplarName;
    }
    return std::string("<unnamed prop>");
}
