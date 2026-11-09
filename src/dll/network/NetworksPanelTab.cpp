#include "NetworksPanelTab.hpp"

#include <array>
#include <filesystem>
#include <wil/resource.h>
#include <wil/win32_helpers.h>

#include "../SC4PlopAndPaintDirector.hpp"
#include "../common/ThumbnailGrid.hpp"
#include "../common/Utils.hpp"
#include "../utils/Logger.h"
#include "rfl/cbor/load.hpp"

namespace {
    // By cISC4NetworkOccupant::eNetworkType.
    constexpr std::array<const char*, 13> kBaseNetworkNames = {
        "Road", "Rail", "Highway", "Street", "Pipe", "Power line", "Avenue",
        "Subway", "Light rail", "Monorail", "One-way road", "RHW", "Ground highway"};

    uint64_t ThumbnailKey(const OverrideNetwork& network) { return MakeGIKey(0, network.starterPiece); }
}

NetworksPanelTab::NetworksPanelTab(SC4PlopAndPaintDirector* director, cIGZImGuiService* imguiService)
    : PanelTab(director, nullptr, nullptr, nullptr, imguiService) {
    try {
        const auto plugins =
            std::filesystem::path(wil::GetModuleFileNameW(wil::GetModuleInstanceHandle()).get()).parent_path();
        const auto path = plugins / "networks.cbor";
        if (!std::filesystem::exists(path)) {
            return;
        }
        auto result = rfl::cbor::load<NetworksCache, rfl::DefaultIfMissing>(path.string());
        if (!result) {
            LOG_WARN("NetworksPanelTab: could not read {}: {}", path.string(), result.error().what());
            return;
        }
        cache_ = std::move(*result);
        // A network without rows comes from an older cache builder and cannot be dragged.
        std::erase_if(cache_.networks, [](const OverrideNetwork& network) { return network.rows.empty(); });
        thumbnails_.Load(plugins / "network_thumbnails.bin");
    }
    catch (const std::exception& e) {
        LOG_WARN("NetworksPanelTab: could not load the override networks: {}", e.what());
        return;
    }

    // Groups in the order of the cache, which is the order of the game's menus.
    for (size_t i = 0; i < cache_.networks.size(); ++i) {
        const std::string& name = cache_.networks[i].group.empty() ? "Other" : cache_.networks[i].group;
        auto group = std::ranges::find(groups_, name, &Group::name);
        if (group == groups_.end()) {
            group = groups_.insert(groups_.end(), Group{name, {}});
        }
        group->networks.push_back(i);
    }
    LOG_INFO("NetworksPanelTab: {} override networks in {} groups", cache_.networks.size(), groups_.size());
}

void NetworksPanelTab::OnDeviceReset(const uint32_t deviceGeneration) {
    if (deviceGeneration != lastDeviceGeneration_) {
        thumbnailCache_.OnDeviceReset();
        lastDeviceGeneration_ = deviceGeneration;
    }
}

ImGuiTexture NetworksPanelTab::LoadTexture_(const uint64_t key, const float tileSize) {
    ImGuiTexture texture;
    if (!imguiService_) {
        return texture;
    }
    if (const auto data = thumbnails_.LoadThumbnail(key, static_cast<uint32_t>(tileSize))) {
        texture.Create(imguiService_, data->width, data->height, data->rgba.data());
    }
    return texture;
}

void NetworksPanelTab::RenderTile_(const OverrideNetwork& network, const bool active) {
    const uint64_t key = ThumbnailKey(network);
    const auto texture = thumbnailCache_.Get(key);
    if (!texture && thumbnails_.HasThumbnail(key)) {
        thumbnailCache_.Request(key);
    }
    RenderGridTile_(texture);

    // The name on a dark band at the foot of the tile. Pictures of starter pieces look much alike.
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float fontSize = ImGui::GetFontSize() * 0.8f;
    const float bandTop = max.y - (fontSize * 2.0f + 6.0f);
    drawList->AddRectFilled(ImVec2(min.x, bandTop), max, IM_COL32(0, 0, 0, 170), ThumbnailUi::kCornerRounding,
                            ImDrawFlags_RoundCornersBottom);
    const ImVec4 clip(min.x + 3.0f, bandTop + 2.0f, max.x - 3.0f, max.y - 2.0f);
    drawList->AddText(ImGui::GetFont(), fontSize, ImVec2(clip.x, clip.y), IM_COL32(255, 255, 255, 255),
                      network.name.c_str(), nullptr, clip.z - clip.x, &clip);
    if (network.rows.size() > 1) {
        char wide[16];
        std::snprintf(wide, sizeof(wide), "x%zu", network.rows.size());
        const ImVec2 size = ImGui::CalcTextSize(wide);
        const ImVec2 badgeMin(max.x - size.x - 9.0f, min.y + 3.0f);
        drawList->AddRectFilled(badgeMin, ImVec2(max.x - 3.0f, min.y + size.y + 5.0f), IM_COL32(32, 32, 32, 220), 3.0f);
        drawList->AddText(ImVec2(badgeMin.x + 3.0f, badgeMin.y + 1.0f), IM_COL32(220, 220, 220, 255), wide);
    }
    ThumbnailGrid::DecorateLastTile(active, false);

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(network.name.c_str());
        if (network.baseNetwork < kBaseNetworkNames.size()) {
            ImGui::TextDisabled("Drags with the %s tool", kBaseNetworkNames[network.baseNetwork]);
        }
        if (network.rows.size() > 1) {
            ImGui::TextDisabled("%zu tiles wide. The other rows follow straight drags only.", network.rows.size());
        }
        ImGui::EndTooltip();
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        director_->StartOverrideNetworkDrag(network);
    }
}

void NetworksPanelTab::OnRender() {
    if (cache_.networks.empty()) {
        ImGui::TextWrapped("No override networks found. Run the cache builder to make networks.cbor in the Plugins "
                           "folder. You must have the Network Addon Mod.");
        return;
    }

    const bool canDrag = SC4PlopAndPaintDirector::CanDragOverrideNetworks();
    const uint32_t activePiece = director_->ActiveOverrideNetwork();
    const auto active = std::ranges::find(cache_.networks, activePiece, &OverrideNetwork::starterPiece);
    ImGui::AlignTextToFramePadding();
    if (!canDrag) {
        ImGui::TextWrapped("Your NAM DLL cannot drag override networks directly. Install a NAM DLL with direct drag.");
    }
    else if (activePiece != 0 && active != cache_.networks.end()) {
        ImGui::Text("Dragging %s", active->name.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop")) {
            director_->StopOverrideNetworkDrag();
        }
    }
    else {
        ImGui::TextUnformatted("Click a network, then drag in the city. No starter piece is necessary.");
    }
    filter_.Draw("Search", ImGui::GetFontSize() * 14.0f);

    const float tileSize = ImGui::GetFontSize() * 7.0f;
    size_t visible = 0;
    std::vector<size_t> shown;
    ImGui::BeginChild("Networks");
    ImGui::BeginDisabled(!canDrag);
    for (const Group& group : groups_) {
        shown.clear();
        const bool groupMatches = filter_.PassFilter(group.name.c_str());
        for (const size_t index : group.networks) {
            if (groupMatches || filter_.PassFilter(cache_.networks[index].name.c_str())) {
                shown.push_back(index);
            }
        }
        if (shown.empty()) {
            continue;
        }
        ImGui::PushID(group.name.c_str());
        char header[160];
        std::snprintf(header, sizeof(header), "%s (%zu)###group", group.name.c_str(), shown.size());
        if (ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) {
            visible += ThumbnailGrid::Render(
                shown.size(), tileSize, [&](const size_t i) { return ThumbnailKey(cache_.networks[shown[i]]); },
                [&](const size_t i) {
                    const OverrideNetwork& network = cache_.networks[shown[i]];
                    RenderTile_(network, network.starterPiece == activePiece);
                });
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    ImGui::EndChild();

    thumbnailCache_.FitVisible(visible + Cache::kMaxLoadPerFrame);
    thumbnailCache_.ProcessLoadQueue([this, tileSize](const uint64_t key) { return LoadTexture_(key, tileSize); });
}
