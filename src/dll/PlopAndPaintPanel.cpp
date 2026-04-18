#include "PlopAndPaintPanel.hpp"

#include "cIGZPersistResourceManager.h"
#include "decals/DecalPanelTab.hpp"
#include "decals/DecalRepository.hpp"
#include "flora/FloraCollectionsPanelTab.hpp"
#include "flora/FloraPanelTab.hpp"
#include "imgui_impl_win32.h"
#include "lots/BuildingsPanelTab.hpp"
#include "lots/OccupantGroups.hpp"
#include "props/FamiliesPanelTab.hpp"
#include "props/PropPanelTab.hpp"


PlopAndPaintPanel::PlopAndPaintPanel(SC4PlopAndPaintDirector* director,
                           LotRepository* lots,
                           PropRepository* props,
                           FloraRepository* flora,
                           FavoritesRepository* favorites,
                           DecalRepository* decals,
                           cIGZPersistResourceManager* pRM,
                           cIGZImGuiService* imguiService)
    : director_(director), imguiService_(imguiService) {
    tabs_.push_back(std::make_unique<BuildingsPanelTab>(director_, lots, props, favorites, imguiService_));
    tabs_.push_back(std::make_unique<PropPanelTab>(director_, lots, props, favorites, imguiService_));
    tabs_.push_back(std::make_unique<FamiliesPanelTab>(director_, lots, props, favorites, imguiService_));
    tabs_.push_back(std::make_unique<FloraPanelTab>(director_, flora, favorites, imguiService_));
    tabs_.push_back(std::make_unique<FloraCollectionsPanelTab>(director_, flora, favorites, imguiService_));
    if (decals) {
        tabs_.push_back(std::make_unique<DecalPanelTab>(director_, decals, pRM, imguiService_));
    }
}

void PlopAndPaintPanel::OnRender() {
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

void PlopAndPaintPanel::SetOpen(const bool open) {
    isOpen_ = open;
}

bool PlopAndPaintPanel::IsOpen() const {
    return isOpen_;
}

void PlopAndPaintPanel::Shutdown() const {
    for (const auto& tab : tabs_) {
        tab->OnShutdown();
    }
}
