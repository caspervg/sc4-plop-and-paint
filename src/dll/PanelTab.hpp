#pragma once
#include <cstdint>

#include "FavoritesRepository.hpp"
#include "LotRepository.hpp"
#include "PropRepository.hpp"
#include "SC4PlopAndPaintDirector.hpp"
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

    SC4PlopAndPaintDirector* director_;  // game actions only
    LotRepository*       lots_;
    PropRepository*      props_;
    FavoritesRepository* favorites_;
    cIGZImGuiService*    imguiService_;
};
