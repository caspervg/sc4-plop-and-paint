#include "LotPlopPanel.hpp"

#include "BuildingsPanelTab.hpp"
#include "imgui_impl_win32.h"
#include "OccupantGroups.hpp"
#include "PalettesPanelTab.hpp"
#include "PropPanelTab.hpp"
#include "spdlog/spdlog.h"


LotPlopPanel::LotPlopPanel(SC4AdvancedLotPlopDirector* director, cIGZImGuiService* imguiService)
    : director_(director), imguiService_(imguiService) {
    tabs_.push_back(std::make_unique<BuildingsPanelTab>(director_, imguiService_));
    tabs_.push_back(std::make_unique<PropPanelTab>(director_, imguiService_));
    tabs_.push_back(std::make_unique<PalettesPanelTab>(director_, imguiService_));
}

void LotPlopPanel::OnRender() {
    if (!isOpen_) {
        return;
    }

    ImGui::Begin("Advanced Plopping & Painting", &isOpen_, ImGuiWindowFlags_NoNavFocus);
    if (!isOpen_) {
        ImGui::End();
        return;
    }

    if (imguiService_) {
        const uint32_t currentGen = imguiService_->GetDeviceGeneration();
        if (currentGen != lastDeviceGeneration_) {
            for (const auto& tab : tabs_) {
                tab->OnDeviceReset(currentGen);
            }
            lastDeviceGeneration_ = currentGen;
        }
    }

    // Tab bar
    if (ImGui::BeginTabBar("AdvancedPlopTabs")) {
        for (const auto& tab : tabs_) {
            if (ImGui::BeginTabItem(tab->GetTabName())) {
                tab->OnRender();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void LotPlopPanel::SetOpen(const bool open) {
    isOpen_ = open;
}

bool LotPlopPanel::IsOpen() const {
    return isOpen_;
}

void LotPlopPanel::Shutdown() const {
    for (const auto& tab : tabs_) {
        tab->OnShutdown();
    }
}
