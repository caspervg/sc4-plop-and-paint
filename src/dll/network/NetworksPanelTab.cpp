#include "NetworksPanelTab.hpp"

#include <algorithm>
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
    constexpr size_t kMaxRecent = 12;

    // The game's own drag tools, in the order of its menus.
    struct BaseNetwork {
        uint32_t type;  // cISC4NetworkOccupant::eNetworkType
        const char* name;
    };
    constexpr BaseNetwork kBaseNetworks[] = {
        {3, "Street"},        {0, "Road"},           {10, "One-way road"}, {6, "Avenue"},
        {11, "RHW"},          {12, "Ground highway"}, {2, "Elevated highway"}, {1, "Rail"},
        {8, "Elevated rail"}, {9, "Monorail"},       {7, "Subway"},
    };

    uint64_t NetworkKey(const OverrideNetwork& network) { return MakeGIKey(0, network.starterPiece); }
    uint64_t PieceKey(const NetworkPiece& piece) { return MakeGIKey(1, piece.id); }

    float TileSize() { return ImGui::GetFontSize() * 7.0f; }
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
        std::erase_if(cache_.buttons, [](const NetworkPieceButton& button) { return button.pieces.empty(); });
        thumbnails_.Load(plugins / "network_thumbnails.bin");
    }
    catch (const std::exception& e) {
        LOG_WARN("NetworksPanelTab: could not load the networks: {}", e.what());
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
    for (NetworkPieceButton& button : cache_.buttons) {
        if (button.path.empty()) {
            button.path.emplace_back("Other");
        }
        if (std::ranges::find(menus_, button.path.front()) == menus_.end()) {
            menus_.push_back(button.path.front());
        }
    }
    LOG_INFO("NetworksPanelTab: {} override networks, {} menu buttons with pieces", cache_.networks.size(),
             cache_.buttons.size());
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

void NetworksPanelTab::RenderTileFace_(const uint64_t key, const std::string& name, const char* badge,
                                       const bool active) {
    const auto texture = thumbnailCache_.Get(key);
    if (!texture && thumbnails_.HasThumbnail(key)) {
        thumbnailCache_.Request(key);
    }
    RenderGridTile_(texture);

    // The name on a dark band at the foot of the tile. Pictures of network pieces look much alike.
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float fontSize = ImGui::GetFontSize() * 0.8f;
    const float bandTop = max.y - (fontSize * 2.0f + 6.0f);
    drawList->AddRectFilled(ImVec2(min.x, bandTop), max, IM_COL32(0, 0, 0, 170), ThumbnailUi::kCornerRounding,
                            ImDrawFlags_RoundCornersBottom);
    const ImVec4 clip(min.x + 3.0f, bandTop + 2.0f, max.x - 3.0f, max.y - 2.0f);
    drawList->AddText(ImGui::GetFont(), fontSize, ImVec2(clip.x, clip.y), IM_COL32(255, 255, 255, 255), name.c_str(),
                      nullptr, clip.z - clip.x, &clip);
    if (badge) {
        const ImVec2 size = ImGui::CalcTextSize(badge);
        const ImVec2 badgeMin(max.x - size.x - 9.0f, min.y + 3.0f);
        drawList->AddRectFilled(badgeMin, ImVec2(max.x - 3.0f, min.y + size.y + 5.0f), IM_COL32(32, 32, 32, 220), 3.0f);
        drawList->AddText(ImVec2(badgeMin.x + 3.0f, badgeMin.y + 1.0f), IM_COL32(220, 220, 220, 255), badge);
    }
    ThumbnailGrid::DecorateLastTile(active, false);
}

void NetworksPanelTab::RenderNetworkTile_(const OverrideNetwork& network, const bool active) {
    char wide[16] = {};
    if (network.rows.size() > 1) {
        std::snprintf(wide, sizeof(wide), "x%zu", network.rows.size());
    }
    RenderTileFace_(NetworkKey(network), network.name, wide[0] ? wide : nullptr, active);

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

void NetworksPanelTab::RenderPieceTile_(const PieceRef ref, const bool active) {
    const NetworkPieceButton& button = cache_.buttons[ref.first];
    const NetworkPiece& piece = button.pieces[ref.second];
    RenderTileFace_(PieceKey(piece), piece.name, nullptr, active);

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(piece.name.c_str());
        ImGui::TextDisabled("%s, %s", button.path.front().c_str(), button.name.c_str());
        if (piece.cost > 0) {
            ImGui::TextDisabled("Cost: %u", piece.cost);
        }
        if (piece.rotations > 1) {
            ImGui::TextDisabled("Home and End turn the piece (%u positions).", piece.rotations);
        }
        ImGui::TextDisabled("Tab shows the next piece of this button.");
        ImGui::EndTooltip();
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && director_->StartNetworkPiece(button.id, piece.id)) {
        std::erase(recent_, ref);
        recent_.insert(recent_.begin(), ref);
        if (recent_.size() > kMaxRecent) {
            recent_.pop_back();
        }
    }
}

bool NetworksPanelTab::PieceMatches_(const NetworkPieceButton& button, const NetworkPiece& piece) const {
    return filter_.PassFilter(piece.name.c_str()) || filter_.PassFilter(button.name.c_str());
}

void NetworksPanelTab::RenderStatus_(const bool canDrag) {
    ImGui::AlignTextToFramePadding();
    const uint32_t dragPiece = director_->ActiveOverrideNetwork();
    const uint32_t plopPiece = director_->ActiveNetworkPiece();
    std::string active;
    if (const int base = director_->ActiveBaseNetwork(); base >= 0) {
        if (const auto network = std::ranges::find(kBaseNetworks, static_cast<uint32_t>(base), &BaseNetwork::type);
            network != std::end(kBaseNetworks)) {
            active = std::string("Dragging ") + network->name;
        }
    }
    else if (dragPiece != 0) {
        if (const auto network = std::ranges::find(cache_.networks, dragPiece, &OverrideNetwork::starterPiece);
            network != cache_.networks.end()) {
            active = "Dragging " + network->name;
        }
    }
    else if (plopPiece != 0) {
        for (const NetworkPieceButton& button : cache_.buttons) {
            if (const auto piece = std::ranges::find(button.pieces, plopPiece, &NetworkPiece::id);
                piece != button.pieces.end()) {
                active = "Placing " + piece->name;
                break;
            }
        }
    }
    if (!active.empty()) {
        ImGui::TextUnformatted(active.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop")) {
            director_->StopNetworkTool();
        }
    }
    else if (!canDrag) {
        ImGui::TextUnformatted("Your NAM DLL cannot drag override networks. Base networks and pieces do not need it.");
    }
    else {
        ImGui::TextUnformatted("Click a network to drag it, or a piece to place it.");
    }
}

void NetworksPanelTab::RenderDragView_(const bool canDrag) {
    const uint32_t activePiece = director_->ActiveOverrideNetwork();
    std::vector<size_t> shown;
    ImGui::BeginChild("Networks");

    // The plain networks first. They have no picture, so they are buttons that wrap.
    if (ImGui::CollapsingHeader("Base networks", ImGuiTreeNodeFlags_DefaultOpen)) {
        const int activeBase = director_->ActiveBaseNetwork();
        const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        bool first = true;
        for (const BaseNetwork& network : kBaseNetworks) {
            if (!filter_.PassFilter(network.name) && !filter_.PassFilter("Base networks")) {
                continue;
            }
            const float width = ImGui::CalcTextSize(network.name).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            if (!first && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width < right) {
                ImGui::SameLine();
            }
            first = false;
            const bool active = activeBase == static_cast<int>(network.type);
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }
            if (ImGui::Button(network.name)) {
                director_->StartBaseNetworkDrag(network.type);
            }
            if (active) {
                ImGui::PopStyleColor();
            }
        }
    }
    if (cache_.networks.empty()) {
        ImGui::TextWrapped("No override networks found. Run the cache builder again with the Network Addon Mod in "
                           "your Plugins folder.");
    }
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
            visibleTiles_ += ThumbnailGrid::Render(
                shown.size(), TileSize(), [&](const size_t i) { return NetworkKey(cache_.networks[shown[i]]); },
                [&](const size_t i) {
                    const OverrideNetwork& network = cache_.networks[shown[i]];
                    RenderNetworkTile_(network, network.starterPiece == activePiece);
                });
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    ImGui::EndChild();
}

void NetworksPanelTab::RenderButtonList_(const std::vector<size_t>& buttons, const std::vector<size_t>& matches) {
    if (!recent_.empty()) {
        char label[48];
        std::snprintf(label, sizeof(label), "Recent (%zu)", recent_.size());
        if (ImGui::Selectable(label, selectedButton_ == kRecent)) {
            selectedButton_ = kRecent;
        }
    }
    const std::string* menu = nullptr;
    for (size_t i = 0; i < buttons.size(); ++i) {
        const NetworkPieceButton& button = cache_.buttons[buttons[i]];
        // The menu, and the submenus below it when there are some, as a heading above its buttons.
        if (!menu || button.path.front() != *menu || button.path.size() > 1) {
            if (!menu || button.path != cache_.buttons[buttons[i - 1]].path) {
                std::string heading = button.path.front();
                for (size_t level = 1; level < button.path.size(); ++level) {
                    heading += " / " + button.path[level];
                }
                ImGui::SeparatorText(heading.c_str());
            }
            menu = &button.path.front();
        }
        ImGui::PushID(static_cast<int>(buttons[i]));
        char label[200];
        std::snprintf(label, sizeof(label), "%s (%zu)", button.name.c_str(), matches[i]);
        if (ImGui::Selectable(label, selectedButton_ == buttons[i])) {
            selectedButton_ = buttons[i];
        }
        if (ImGui::IsItemHovered() && ImGui::CalcTextSize(label).x > ImGui::GetContentRegionAvail().x) {
            ImGui::SetTooltip("%s", button.name.c_str());
        }
        ImGui::PopID();
    }
}

void NetworksPanelTab::RenderPiecesView_() {
    if (cache_.buttons.empty()) {
        ImGui::TextWrapped("No pieces found. Run the cache builder again to list the pieces of the network menus.");
        return;
    }

    // The game's menus as a first cut.
    if (ImGui::RadioButton("All menus", menu_ < 0)) {
        menu_ = -1;
    }
    for (int i = 0; i < static_cast<int>(menus_.size()); ++i) {
        ImGui::SameLine();
        if (ImGui::RadioButton(menus_[i].c_str(), menu_ == i)) {
            menu_ = i;
        }
    }

    // The buttons that are in the chosen menu and have a piece the search finds.
    std::vector<size_t> buttons;
    std::vector<size_t> matches;
    for (size_t i = 0; i < cache_.buttons.size(); ++i) {
        const NetworkPieceButton& button = cache_.buttons[i];
        if (menu_ >= 0 && button.path.front() != menus_[menu_]) {
            continue;
        }
        const auto count = static_cast<size_t>(std::ranges::count_if(
            button.pieces, [&](const NetworkPiece& piece) { return PieceMatches_(button, piece); }));
        if (count > 0) {
            buttons.push_back(i);
            matches.push_back(count);
        }
    }
    if (buttons.empty()) {
        ImGui::TextDisabled("No piece has this name.");
        return;
    }
    if (selectedButton_ == kRecent ? recent_.empty() : std::ranges::find(buttons, selectedButton_) == buttons.end()) {
        selectedButton_ = buttons.front();
    }

    ImGui::BeginChild("Buttons", ImVec2(ImGui::GetFontSize() * 17.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    RenderButtonList_(buttons, matches);
    ImGui::EndChild();
    ImGui::SameLine();

    std::vector<PieceRef> shown;
    if (selectedButton_ == kRecent) {
        shown = recent_;
    }
    else {
        const NetworkPieceButton& button = cache_.buttons[selectedButton_];
        for (size_t i = 0; i < button.pieces.size(); ++i) {
            if (PieceMatches_(button, button.pieces[i])) {
                shown.emplace_back(selectedButton_, i);
            }
        }
    }
    const uint32_t activePiece = director_->ActiveNetworkPiece();
    ImGui::BeginChild("Pieces");
    visibleTiles_ += ThumbnailGrid::Render(
        shown.size(), TileSize(),
        [&](const size_t i) { return PieceKey(cache_.buttons[shown[i].first].pieces[shown[i].second]); },
        [&](const size_t i) {
            RenderPieceTile_(shown[i], cache_.buttons[shown[i].first].pieces[shown[i].second].id == activePiece);
        });
    ImGui::EndChild();
}

void NetworksPanelTab::OnRender() {
    if (cache_.networks.empty() && cache_.buttons.empty()) {
        ImGui::TextWrapped("No network items found. Run the cache builder to make networks.cbor in the Plugins "
                           "folder. You must have the Network Addon Mod.");
        return;
    }

    const bool canDrag = SC4PlopAndPaintDirector::CanDragOverrideNetworks();
    RenderStatus_(canDrag);
    filter_.Draw("Search", ImGui::GetFontSize() * 14.0f);

    visibleTiles_ = 0;
    if (ImGui::BeginTabBar("NetworkViews")) {
        if (ImGui::BeginTabItem("Drag")) {
            RenderDragView_(canDrag);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Pieces")) {
            RenderPiecesView_();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    const float tileSize = TileSize();
    thumbnailCache_.FitVisible(visibleTiles_ + Cache::kMaxLoadPerFrame);
    thumbnailCache_.ProcessLoadQueue([this, tileSize](const uint64_t key) { return LoadTexture_(key, tileSize); });
}
