#pragma once
#include <cstdint>
#include <optional>

#include "../SC4PlopAndPaintDirector.hpp"
#include "../favorites/FavoritesRepository.hpp"
#include "../lots/LotRepository.hpp"
#include "../props/PropRepository.hpp"
#include "Constants.hpp"
#include "ThumbnailUi.hpp"
#include "imgui.h"
#include "imgui_internal.h"


class cIGZImGuiService;

class PanelTab {
public:
    PanelTab(SC4PlopAndPaintDirector* director,
             LotRepository* lots,
             PropRepository* props,
             FavoritesRepository* favorites,
             cIGZImGuiService* imguiService)
        : director_(director)
        , lots_(lots)
        , props_(props)
        , favorites_(favorites)
        , imguiService_(imguiService) {}

    virtual ~PanelTab() = default;

    [[nodiscard]] virtual const char* GetTabName() const = 0;

    virtual void OnRender() = 0;

    virtual void OnDeviceReset(uint32_t deviceGeneration) = 0;

    // Called before the ImGui service is released during shutdown.
    // Subclasses should release any textures/resources that depend on the service.
    virtual void OnShutdown() {}

    // Abandons all textures without calling the service.
    // Use during DLL teardown when the service may already be destroyed.
    virtual void Abandon() {}

protected:
    static void ReleaseImGuiInputCapture_() {
        if (ImGui::GetCurrentContext()) {
            ImGui::ClearActiveID();
            ImGui::SetWindowFocus(nullptr);
        }
    }

    void RenderPropStripperControls_() const {
        if (!director_) {
            return;
        }

        ImGui::SameLine();
        uint32_t sourceFlags = director_->GetPropStripperSources();
        bool cityEnabled = (sourceFlags & PropStripperInputControl::SourceFlagCity) != 0;
        bool lotEnabled = (sourceFlags & PropStripperInputControl::SourceFlagLot) != 0;
        bool streetEnabled = (sourceFlags & PropStripperInputControl::SourceFlagStreet) != 0;
        bool sourceChanged = false;

        sourceChanged = ImGui::Checkbox("City##PropStripCity", &cityEnabled) || sourceChanged;
        ImGui::SameLine(0.0f, 6.0f);
        sourceChanged = ImGui::Checkbox("Lot##PropStripLot", &lotEnabled) || sourceChanged;
        ImGui::SameLine(0.0f, 6.0f);
        sourceChanged = ImGui::Checkbox("Street##PropStripStreet", &streetEnabled) || sourceChanged;

        if (sourceChanged) {
            uint32_t newFlags = PropStripperInputControl::SourceFlagNone;
            if (cityEnabled) {
                newFlags |= PropStripperInputControl::SourceFlagCity;
            }
            if (lotEnabled) {
                newFlags |= PropStripperInputControl::SourceFlagLot;
            }
            if (streetEnabled) {
                newFlags |= PropStripperInputControl::SourceFlagStreet;
            }
            director_->SetPropStripperSources(newFlags);
        }

        if (director_->IsPropStripping()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Stop stripping")) {
                director_->StopPropStripping();
            }
        }
        else {
            ImGui::SameLine();
            const bool hasAnySource = cityEnabled || lotEnabled || streetEnabled;
            if (!hasAnySource) {
                ImGui::BeginDisabled();
            }
            if (ImGui::SmallButton("Strip props")) {
                ReleaseImGuiInputCapture_();
                director_->StartPropStripping();
            }
            if (!hasAnySource) {
                ImGui::EndDisabled();
            }
            if (ImGui::IsItemHovered()) {
                if (hasAnySource) {
                    ImGui::SetTooltip("Click props to remove them one by one.\nPress B to toggle brush mode.\nHold left mouse in brush mode to strip within the preview radius.\nCtrl+Z restores removed city, lot, and street props.\nESC stops stripping.");
                }
                else {
                    ImGui::SetTooltip("Enable at least one prop source to start stripping.");
                }
            }
        }
    }

    void RenderFloraStripperControls_() const {
        if (!director_) {
            return;
        }

        if (director_->IsFloraStripping()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Stop stripping")) {
                director_->StopFloraStripping();
            }
        }
        else {
            ImGui::SameLine();
            if (ImGui::SmallButton("Strip flora")) {
                ReleaseImGuiInputCapture_();
                director_->StartFloraStripping();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Click flora in the city to remove it one by one.\nPress B to toggle brush mode.\nHold left mouse in brush mode to strip within the preview radius.\nCtrl+Z to undo, ESC to stop.");
            }
        }
    }

    void RenderThumbnail_(const std::optional<void*> textureId,
                          const ImVec2 size = ImVec2(UI::kIconSize, UI::kIconSize)) const {
        ThumbnailUi::Render(
            textureId,
            size,
            director_ ? director_->GetThumbnailBackgroundColor() : IM_COL32(0, 0, 0, 0),
            director_ ? director_->GetThumbnailBorderColor() : IM_COL32(0, 0, 0, 0));
    }

    SC4PlopAndPaintDirector* director_;  // game actions only
    LotRepository*       lots_;
    PropRepository*      props_;
    FavoritesRepository* favorites_;
    cIGZImGuiService*    imguiService_;
};
