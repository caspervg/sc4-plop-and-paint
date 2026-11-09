#pragma once
#include "imgui.h"

#include "../common/PanelTab.hpp"
#include "../../shared/networks.hpp"

// A button for every override network of the Network Addon Mod. A click starts the game's drag tool for
// the base network, and the NAM DLL turns each drag into the override network, with no starter piece.
class NetworksPanelTab final : public PanelTab {
public:
    NetworksPanelTab(SC4PlopAndPaintDirector* director, cIGZImGuiService* imguiService);

    [[nodiscard]] const char* GetTabName() const override { return "Networks"; }
    void OnRender() override;
    void OnDeviceReset(uint32_t) override {}

private:
    NetworksCache cache_;
    ImGuiTextFilter filter_;
};
