#include "NetworksPanelTab.hpp"

#include <array>
#include <filesystem>
#include <wil/resource.h>
#include <wil/win32_helpers.h>

#include "../SC4PlopAndPaintDirector.hpp"
#include "../utils/Logger.h"
#include "rfl/cbor/load.hpp"

namespace {
    // By cISC4NetworkOccupant::eNetworkType.
    constexpr std::array<const char*, 13> kBaseNetworkNames = {
        "Road", "Rail", "Highway", "Street", "Pipe", "Power line", "Avenue",
        "Subway", "Light rail", "Monorail", "One-way road", "RHW", "Ground highway"};
}

NetworksPanelTab::NetworksPanelTab(SC4PlopAndPaintDirector* director, cIGZImGuiService* imguiService)
    : PanelTab(director, nullptr, nullptr, nullptr, imguiService) {
    try {
        const auto path = std::filesystem::path(wil::GetModuleFileNameW(wil::GetModuleInstanceHandle()).get())
                              .parent_path() / "networks.cbor";
        if (!std::filesystem::exists(path)) {
            return;
        }
        if (auto result = rfl::cbor::load<NetworksCache, rfl::DefaultIfMissing>(path.string())) {
            cache_ = std::move(*result);
            LOG_INFO("NetworksPanelTab: {} override networks", cache_.networks.size());
        }
        else {
            LOG_WARN("NetworksPanelTab: could not read {}: {}", path.string(), result.error().what());
        }
    }
    catch (const std::exception& e) {
        LOG_WARN("NetworksPanelTab: could not load the override networks: {}", e.what());
    }
}

void NetworksPanelTab::OnRender() {
    if (cache_.networks.empty()) {
        ImGui::TextWrapped("No override networks found. Run the cache builder to make networks.cbor in the Plugins "
                           "folder. You must have the Network Addon Mod.");
        return;
    }
    const bool canDrag = SC4PlopAndPaintDirector::CanDragOverrideNetworks();
    if (!canDrag) {
        ImGui::TextWrapped("Your NAM DLL cannot drag override networks directly. Install a NAM DLL with direct drag.");
    }
    else {
        ImGui::TextWrapped("Click a network, then drag in the city. No starter piece is necessary.");
    }
    filter_.Draw("Search");

    const uint32_t activeTile = director_->ActiveOverrideNetworkTile();
    ImGui::BeginChild("Networks");
    ImGui::BeginDisabled(!canDrag);
    for (uint32_t base = 0; base < kBaseNetworkNames.size(); ++base) {
        bool open = false;
        bool any = false;
        for (const auto& network : cache_.networks) {
            if (network.baseNetwork != base || !filter_.PassFilter(network.name.c_str())) {
                continue;
            }
            if (!any) {
                any = true;
                open = ImGui::CollapsingHeader(kBaseNetworkNames[base], ImGuiTreeNodeFlags_DefaultOpen);
            }
            if (!open) {
                break;
            }
            ImGui::PushID(static_cast<int>(network.tile));
            if (ImGui::Selectable(network.name.c_str(), network.tile == activeTile)) {
                director_->StartOverrideNetworkDrag(network.tile, network.baseNetwork);
            }
            ImGui::PopID();
        }
    }
    ImGui::EndDisabled();
    ImGui::EndChild();
}
