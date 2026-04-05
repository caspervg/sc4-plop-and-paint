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

        static constexpr const char* kPropStripperTargets[] = {
            "City",
            "Lot",
            "Street"
        };

        ImGui::SameLine();
        int targetIndex = static_cast<int>(director_->GetPropStripperTarget());
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::Combo("##PropStripTarget", &targetIndex, kPropStripperTargets, IM_ARRAYSIZE(kPropStripperTargets))) {
            director_->SetPropStripperTarget(static_cast<PropStripperInputControl::TargetKind>(targetIndex));
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Choose which prop bucket the stripper queries around the cursor.");
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
                ImGui::SetTooltip("Click props to remove them one by one.\nPress B to toggle brush mode.\nHold left mouse in brush mode to strip within the preview radius.\nCtrl+Z restores city props only.\nESC stops stripping.");
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
