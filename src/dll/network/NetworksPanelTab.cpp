#include "NetworksPanelTab.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <wil/resource.h>
#include <wil/win32_helpers.h>

#include "../SC4PlopAndPaintDirector.hpp"
#include "../common/Constants.hpp"
#include "../common/PanelUi.hpp"
#include "../common/ThumbnailGrid.hpp"
#include "../common/Utils.hpp"
#include "../utils/Logger.h"
#include "rfl/cbor/load.hpp"

using PanelUi::ButtonWidth;
using PanelUi::CheckboxWidth;
using PanelUi::Hint;
using PanelUi::kGroupGap;
using PanelUi::NextInRow;
using PanelUi::Tooltip;

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

    const char* BaseNetworkName(const uint32_t type) {
        return type < kBaseNetworkNames.size() ? kBaseNetworkNames[type] : "";
    }

    uint64_t NetworkKey(const OverrideNetwork& network) { return MakeGIKey(0, network.starterPiece); }
    uint64_t PieceKey(const NetworkPiece& piece) { return MakeGIKey(1, piece.id); }
    uint64_t NetworkFavorite(const OverrideNetwork& network) {
        return FavoritesRepository::NetworkFavoriteKey(false, network.starterPiece);
    }
    uint64_t PieceFavorite(const NetworkPiece& piece) { return FavoritesRepository::NetworkFavoriteKey(true, piece.id); }
}

NetworksPanelTab::NetworksPanelTab(SC4PlopAndPaintDirector* director, FavoritesRepository* favorites,
                                   cIGZImGuiService* imguiService)
    : PanelTab(director, nullptr, nullptr, favorites, imguiService) {
    gridView_ = true;  // tiles only, no list
    gridTileScale_ = 1.4f;
    for (const BaseNetwork& network : kBaseNetworks) {
        baseSearch_.push_back(TextSearch::Normalize(std::string(network.name) + " base network"));
    }
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
        OverrideNetwork& network = cache_.networks[i];
        if (network.group.empty()) {
            network.group = "Other";
        }
        auto group = std::ranges::find(groups_, network.group, &Group::name);
        if (group == groups_.end()) {
            group = groups_.insert(groups_.end(), Group{network.group, {}});
        }
        group->networks.push_back(i);
        networkSearch_.push_back(TextSearch::Normalize(network.name + ' ' + network.group + ' ' +
                                                       BaseNetworkName(network.baseNetwork) +
                                                       (network.diagonal ? " diagonal" : "")) +
                                 TextSearch::HexId(network.starterPiece));
        suggester_.Add(networkSearch_.back());
    }
    for (NetworkPieceButton& button : cache_.buttons) {
        if (button.path.empty()) {
            button.path.emplace_back("Other");
        }
        if (std::ranges::find(menus_, button.path.front()) == menus_.end()) {
            menus_.push_back(button.path.front());
        }
        std::string where = button.name;
        for (const std::string& menu : button.path) {
            where += ' ' + menu;
        }
        auto& texts = pieceSearch_.emplace_back();
        for (const NetworkPiece& piece : button.pieces) {
            texts.push_back(TextSearch::Normalize(piece.name + ' ' + where) + TextSearch::HexId(piece.id));
            suggester_.Add(texts.back());
        }
        pieceCount_ += button.pieces.size();
    }
    LOG_INFO("NetworksPanelTab: {} override networks, {} menu buttons with pieces", cache_.networks.size(),
             cache_.buttons.size());
}

void NetworksPanelTab::OnDeviceReset(const uint32_t deviceGeneration) {
    if (deviceGeneration != lastDeviceGeneration_) {
        thumbnailCache_.OnDeviceReset();
        previewCache_.OnDeviceReset();
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

bool NetworksPanelTab::IsFavorite_(const uint64_t favoriteKey) const {
    return favorites_ && favorites_->IsFamilyFavorite(favoriteKey);
}

void NetworksPanelTab::RenderTileMenu_(const uint64_t favoriteKey) const {
    if (favorites_ && ImGui::BeginPopupContextItem("##tileMenu")) {
        if (ImGui::Selectable(IsFavorite_(favoriteKey) ? "Remove from favorites" : "Add to favorites")) {
            favorites_->ToggleFamilyFavorite(favoriteKey);
        }
        ImGui::EndPopup();
    }
}

void NetworksPanelTab::RenderTileFace_(const uint64_t key, const std::string& name, const char* badge,
                                       const bool active, const uint64_t favoriteKey) {
    const auto texture = thumbnailCache_.Get(key);
    if (!texture && thumbnails_.HasThumbnail(key)) {
        thumbnailCache_.Request(key);
    }
    RenderGridTile_(texture);

    // The name on a dark band at the foot of the tile. Pictures of network pieces look much alike.
    // Small tiles have room for one line only.
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float fontSize = ImGui::GetFontSize() * 0.8f;
    const float lines = max.y - min.y >= ImGui::GetFontSize() * 6.0f ? 2.0f : 1.0f;
    const float bandTop = max.y - (fontSize * lines + 6.0f);
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
    ThumbnailGrid::DecorateLastTile(active, IsFavorite_(favoriteKey));
    RenderTileMenu_(favoriteKey);
}

void NetworksPanelTab::RenderNetworkTile_(const OverrideNetwork& network, const bool active) {
    char wide[16] = {};
    if (network.rows.size() > 1) {
        std::snprintf(wide, sizeof(wide), "x%zu", network.rows.size());
    }
    RenderTileFace_(NetworkKey(network), network.name, wide[0] ? wide : nullptr, active, NetworkFavorite(network));
    if (network.diagonal) {
        // A diagonal stroke in the corner where wide networks have their badge. Wide ones never drag on diagonals.
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float size = ImGui::GetFontSize() * 1.1f;
        const ImVec2 max(ImGui::GetItemRectMax().x - 3.0f, ImGui::GetItemRectMin().y + 3.0f + size);
        const ImVec2 min(max.x - size, max.y - size);
        drawList->AddRectFilled(min, max, IM_COL32(32, 32, 32, 220), 3.0f);
        drawList->AddLine(ImVec2(min.x + 4.0f, max.y - 4.0f), ImVec2(max.x - 4.0f, min.y + 4.0f),
                          IM_COL32(220, 220, 220, 255), 2.0f);
    }

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(network.name.c_str());
        if (network.baseNetwork < kBaseNetworkNames.size()) {
            ImGui::TextDisabled("Drags with the %s tool", kBaseNetworkNames[network.baseNetwork]);
        }
        if (network.diagonal) {
            ImGui::TextDisabled("Drags on diagonals too");
        }
        if (network.rows.size() > 1) {
            ImGui::TextDisabled("%zu tiles wide. The other rows follow straight drags only.", network.rows.size());
        }
        RenderLargePreview_(thumbnails_, NetworkKey(network));
        ImGui::EndTooltip();
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        ReleaseImGuiInputCapture_();
        director_->StartOverrideNetworkDrag(network);
    }
}

void NetworksPanelTab::RenderPieceTile_(const PieceRef ref, const bool active) {
    const NetworkPieceButton& button = cache_.buttons[ref.first];
    const NetworkPiece& piece = button.pieces[ref.second];
    RenderTileFace_(PieceKey(piece), piece.name, nullptr, active, PieceFavorite(piece));

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(piece.name.c_str());
        ImGui::TextDisabled("%s, %s", button.path.front().c_str(), button.name.c_str());
        if (piece.cost > 0) {
            ImGui::TextDisabled("Cost: %u", piece.cost);
        }
        RenderLargePreview_(thumbnails_, PieceKey(piece));
        ImGui::EndTooltip();
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        ReleaseImGuiInputCapture_();
        if (director_->StartNetworkPiece(button.id, piece.id)) {
            std::erase(recent_, ref);
            recent_.insert(recent_.begin(), ref);
            if (recent_.size() > kMaxRecent) {
                recent_.pop_back();
            }
        }
    }
}

bool NetworksPanelTab::PieceShown_(const PieceRef ref, const TextSearch::Query& query) const {
    return (!favoritesOnly_ || IsFavorite_(PieceFavorite(cache_.buttons[ref.first].pieces[ref.second]))) &&
           query.Matches(pieceSearch_[ref.first][ref.second]);
}

int NetworksPanelTab::ActiveFilterCount_() const {
    return static_cast<int>(searchBuf_[0] != '\0') + static_cast<int>(favoritesOnly_) +
           static_cast<int>(view_ == View::Pieces && menu_ >= 0);
}

void NetworksPanelTab::ClearFilters_() {
    searchBuf_[0] = '\0';
    favoritesOnly_ = false;
    menu_ = -1;
}

void NetworksPanelTab::RenderFilterUI_() {
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##SearchNetworks", view_ == View::Drag ? "Search networks..." : "Search pieces...",
                             searchBuf_, sizeof(searchBuf_));
    Tooltip(PanelUi::kSearchHelp);

    size_t index = 0;
    if (view_ == View::Pieces && !menus_.empty()) {
        ImGui::SetNextItemWidth(UI::dropdownWidth());
        if (ImGui::BeginCombo("##Menu", menu_ < 0 ? "All menus" : menus_[menu_].c_str())) {
            if (ImGui::Selectable("All menus", menu_ < 0)) {
                menu_ = -1;
            }
            for (int i = 0; i < static_cast<int>(menus_.size()); ++i) {
                ImGui::PushID(i);
                if (ImGui::Selectable(menus_[i].c_str(), menu_ == i)) {
                    menu_ = i;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        Tooltip("Show the pieces of one network menu.");
        ++index;
    }
    NextInRow(index, CheckboxWidth("Favorites only"));
    ImGui::Checkbox("Favorites only", &favoritesOnly_);
    Tooltip(view_ == View::Drag ? "Show only the networks that you starred."
                                : "Show only the pieces that you starred.");
}

void NetworksPanelTab::RenderResultLine_(const size_t shown, const size_t total, const char* items) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Showing %zu of %zu %s", shown, total, items);
    const int active = ActiveFilterCount_();
    if (active > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("%d filter%s on", active, active == 1 ? "" : "s");
    }
    ImGui::SameLine(0.0f, kGroupGap);
    ImGui::BeginDisabled(active == 0);
    if (ImGui::SmallButton("Clear filters")) {
        ClearFilters_();
    }
    ImGui::EndDisabled();
    Tooltip(active == 0 ? "No filters are on." : "Show all items again.");
    if (RenderViewControls_(true)) {
        thumbnailCache_.Clear();
    }
}

std::string NetworksPanelTab::ActiveTool_() const {
    if (const int base = director_->ActiveBaseNetwork(); base >= 0) {
        if (const auto network = std::ranges::find(kBaseNetworks, static_cast<uint32_t>(base), &BaseNetwork::type);
            network != std::end(kBaseNetworks)) {
            return std::string("Dragging ") + network->name;
        }
    }
    if (const uint32_t piece = director_->ActiveOverrideNetwork(); piece != 0) {
        if (const auto network = std::ranges::find(cache_.networks, piece, &OverrideNetwork::starterPiece);
            network != cache_.networks.end()) {
            return "Dragging " + network->name;
        }
    }
    if (const uint32_t id = director_->ActiveNetworkPiece(); id != 0) {
        for (const NetworkPieceButton& button : cache_.buttons) {
            if (const auto piece = std::ranges::find(button.pieces, id, &NetworkPiece::id);
                piece != button.pieces.end()) {
                return "Placing " + piece->name;
            }
        }
    }
    return {};
}

void NetworksPanelTab::RenderToolRow_() {
    const std::string active = ActiveTool_();
    if (active.empty()) {
        Hint(view_ == View::Drag ? "Click a network to drag it in the city. Right-click a network to star it."
                                 : "Click a piece to place it in the city. Right-click a piece to star it.");
        return;
    }
    if (ImGui::SmallButton("Stop")) {
        director_->StopNetworkTool();
    }
    Tooltip("Stop the network tool. What you built stays in the city.");
    ImGui::SameLine();
    PanelUi::SecondaryText("%s", active.c_str());
}

void NetworksPanelTab::RenderNoResults_(const char* text) {
    if (PanelUi::NoResults(text)) {
        ClearFilters_();
    }
    else if (const auto suggestion = suggester_.Suggest(searchBuf_); suggestion && PanelUi::DidYouMean(*suggestion)) {
        std::snprintf(searchBuf_, sizeof(searchBuf_), "%s", suggestion->c_str());
    }
}

void NetworksPanelTab::RenderDragView_(const TextSearch::Query& query) {
    // The base networks cannot be starred, so Favorites only hides them.
    std::vector<size_t> bases;
    if (!favoritesOnly_) {
        for (size_t i = 0; i < std::size(kBaseNetworks); ++i) {
            if (query.Matches(baseSearch_[i])) {
                bases.push_back(i);
            }
        }
    }
    std::vector<std::vector<size_t>> shown(groups_.size());
    size_t count = bases.size();
    for (size_t g = 0; g < groups_.size(); ++g) {
        for (const size_t index : groups_[g].networks) {
            if ((!favoritesOnly_ || IsFavorite_(NetworkFavorite(cache_.networks[index]))) &&
                query.Matches(networkSearch_[index])) {
                shown[g].push_back(index);
            }
        }
        count += shown[g].size();
    }

    RenderResultLine_(count, std::size(kBaseNetworks) + cache_.networks.size(), "networks");
    RenderToolRow_();
    if (count == 0) {
        RenderNoResults_("No networks match these filters.");
        return;
    }

    if (!ImGui::BeginChild("Networks", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                           ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        ImGui::EndChild();
        return;
    }
    // The plain networks first. They have no picture, so they are buttons that wrap.
    if (!bases.empty() && ImGui::CollapsingHeader("Base networks", ImGuiTreeNodeFlags_DefaultOpen)) {
        const int activeBase = director_->ActiveBaseNetwork();
        for (size_t i = 0; i < bases.size(); ++i) {
            const BaseNetwork& network = kBaseNetworks[bases[i]];
            NextInRow(i, ButtonWidth(network.name));
            const bool active = activeBase == static_cast<int>(network.type);
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }
            if (ImGui::Button(network.name)) {
                ReleaseImGuiInputCapture_();
                director_->StartBaseNetworkDrag(network.type);
            }
            if (active) {
                ImGui::PopStyleColor();
            }
        }
    }

    if (cache_.networks.empty()) {
        Hint("No override networks found. Run the cache builder again with the Network Addon Mod in your "
             "Plugins folder.");
    }
    const bool canDrag = SC4PlopAndPaintDirector::CanDragOverrideNetworks();
    if (!canDrag && count > bases.size()) {
        Hint("Your NAM DLL cannot drag override networks. Base networks and pieces work without it.");
    }
    const uint32_t activePiece = director_->ActiveOverrideNetwork();
    const float tileSize = TileSize_();
    ImGui::BeginDisabled(!canDrag);
    for (size_t g = 0; g < groups_.size(); ++g) {
        if (shown[g].empty()) {
            continue;
        }
        const std::vector<size_t>& networks = shown[g];
        ImGui::PushID(groups_[g].name.c_str());
        char header[160];
        std::snprintf(header, sizeof(header), "%s (%zu)###group", groups_[g].name.c_str(), networks.size());
        if (ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) {
            visibleTiles_ += ThumbnailGrid::Render(
                networks.size(), tileSize, [&](const size_t i) { return NetworkKey(cache_.networks[networks[i]]); },
                [&](const size_t i) {
                    const OverrideNetwork& network = cache_.networks[networks[i]];
                    RenderNetworkTile_(network, network.starterPiece == activePiece);
                });
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    ImGui::EndChild();
}

void NetworksPanelTab::RenderButtonList_(const std::vector<size_t>& buttons,
                                         const std::vector<std::vector<size_t>>& pieces, const size_t recentCount) {
    if (recentCount > 0) {
        char label[48];
        std::snprintf(label, sizeof(label), "Recent (%zu)", recentCount);
        if (ImGui::Selectable(label, selectedButton_ == kRecent)) {
            selectedButton_ = kRecent;
        }
    }
    for (size_t i = 0; i < buttons.size(); ++i) {
        const NetworkPieceButton& button = cache_.buttons[buttons[i]];
        // The menu, and the submenus below it when there are some, as a heading above its buttons.
        if (i == 0 || button.path != cache_.buttons[buttons[i - 1]].path) {
            std::string heading = button.path.front();
            for (size_t level = 1; level < button.path.size(); ++level) {
                heading += " / " + button.path[level];
            }
            ImGui::SeparatorText(heading.c_str());
        }
        ImGui::PushID(static_cast<int>(buttons[i]));
        char label[200];
        std::snprintf(label, sizeof(label), "%s (%zu)", button.name.c_str(), pieces[i].size());
        if (ImGui::Selectable(label, selectedButton_ == buttons[i])) {
            selectedButton_ = buttons[i];
        }
        if (ImGui::IsItemHovered() && ImGui::CalcTextSize(label).x > ImGui::GetContentRegionAvail().x) {
            ImGui::SetTooltip("%s", button.name.c_str());
        }
        ImGui::PopID();
    }
}

void NetworksPanelTab::RenderPiecesView_(const TextSearch::Query& query) {
    if (cache_.buttons.empty()) {
        Hint("No pieces found. Run the cache builder again to list the pieces of the network menus.");
        return;
    }

    // The buttons that are in the chosen menu and have a piece that the filters let through.
    std::vector<size_t> buttons;
    std::vector<std::vector<size_t>> pieces;
    size_t count = 0;
    for (size_t b = 0; b < cache_.buttons.size(); ++b) {
        const NetworkPieceButton& button = cache_.buttons[b];
        if (menu_ >= 0 && button.path.front() != menus_[menu_]) {
            continue;
        }
        std::vector<size_t> matches;
        for (size_t p = 0; p < button.pieces.size(); ++p) {
            if (PieceShown_({b, p}, query)) {
                matches.push_back(p);
            }
        }
        if (!matches.empty()) {
            count += matches.size();
            buttons.push_back(b);
            pieces.push_back(std::move(matches));
        }
    }
    std::vector<PieceRef> recent;
    for (const PieceRef ref : recent_) {
        if ((menu_ < 0 || cache_.buttons[ref.first].path.front() == menus_[menu_]) && PieceShown_(ref, query)) {
            recent.push_back(ref);
        }
    }

    RenderResultLine_(count, pieceCount_, "pieces");
    RenderToolRow_();
    if (count == 0) {
        RenderNoResults_("No pieces match these filters.");
        return;
    }

    const auto selected = std::ranges::find(buttons, selectedButton_);
    if (selectedButton_ == kRecent ? recent.empty() : selected == buttons.end()) {
        selectedButton_ = buttons.front();
    }

    // The list keeps at least half of a narrow panel for the pieces. Drag its edge to make it wider.
    const float listWidth = std::min(ImGui::GetFontSize() * 15.0f, ImGui::GetContentRegionAvail().x * 0.45f);
    if (ImGui::BeginChild("Buttons", ImVec2(listWidth, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX)) {
        RenderButtonList_(buttons, pieces, recent.size());
    }
    ImGui::EndChild();
    ImGui::SameLine();

    std::vector<PieceRef> shown;
    if (selectedButton_ == kRecent) {
        shown = std::move(recent);
    }
    else {
        const auto& matches = pieces[std::ranges::find(buttons, selectedButton_) - buttons.begin()];
        for (const size_t p : matches) {
            shown.emplace_back(selectedButton_, p);
        }
    }
    const uint32_t activePiece = director_->ActiveNetworkPiece();
    if (ImGui::BeginChild("Pieces", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        visibleTiles_ += ThumbnailGrid::Render(
            shown.size(), TileSize_(),
            [&](const size_t i) { return PieceKey(cache_.buttons[shown[i].first].pieces[shown[i].second]); },
            [&](const size_t i) {
                RenderPieceTile_(shown[i], cache_.buttons[shown[i].first].pieces[shown[i].second].id == activePiece);
            });
    }
    ImGui::EndChild();
}

void NetworksPanelTab::OnRender() {
    if (cache_.networks.empty() && cache_.buttons.empty()) {
        ImGui::TextWrapped("No network items found. Run the cache builder to make networks.cbor in the Plugins "
                           "folder. You must have the Network Addon Mod.");
        return;
    }

    // The tabs only pick the view. Both views share the search and the filters below them.
    if (ImGui::BeginTabBar("NetworkViews")) {
        if (ImGui::BeginTabItem("Drag networks")) {
            view_ = View::Drag;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Place pieces")) {
            view_ = View::Pieces;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    RenderFilterUI_();
    ImGui::Separator();

    visibleTiles_ = 0;
    const TextSearch::Query query(searchBuf_);
    if (view_ == View::Drag) {
        RenderDragView_(query);
    }
    else {
        RenderPiecesView_(query);
    }

    const float tileSize = TileSize_();
    thumbnailCache_.FitVisible(visibleTiles_ + Cache::kMaxLoadPerFrame);
    thumbnailCache_.ProcessLoadQueue([this, tileSize](const uint64_t key) { return LoadTexture_(key, tileSize); });
}
