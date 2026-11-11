#include "NetworksPanelTab.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <wil/resource.h>
#include <wil/win32_helpers.h>

#include "../SC4PlopAndPaintDirector.hpp"
#include "../common/Constants.hpp"
#include "../common/PanelUi.hpp"
#include "../common/ThumbnailGrid.hpp"
#include "../common/Utils.hpp"
#include "../utils/Logger.h"
#include "imgui_internal.h"
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
    uint64_t VariantKey(const uint32_t id) { return MakeGIKey(1, id); }
    uint64_t IconKey(const uint32_t icon) { return MakeGIKey(2, icon); }
    uint64_t NetworkFavorite(const OverrideNetwork& network) {
        return FavoritesRepository::NetworkFavoriteKey(false, network.starterPiece);
    }
    uint64_t PieceFavorite(const NetworkPiece& piece) { return FavoritesRepository::NetworkFavoriteKey(true, piece.id); }

    // The NAM's Network Eraser, the one piece of its menu button. The tab has a button of its own for it.
    constexpr uint32_t kNetworkEraserPiece = 0x00005024;

    // The name of a menu button as the list shows it. A cache from an older builder has no label.
    const std::string& ButtonLabel(const NetworkPieceButton& button) {
        return button.label.empty() ? button.name : button.label;
    }


    // The name of a network as the game's menus call it.
    const char* NetworkTypeName(const uint32_t type) {
        const auto menu = std::ranges::find(kBaseNetworks, type, &BaseNetwork::type);
        return menu != std::end(kBaseNetworks) ? menu->name : BaseNetworkName(type);
    }

    std::vector<std::string> Words(const std::string& text) {
        std::vector<std::string> words;
        std::istringstream stream(text);
        for (std::string word; stream >> word;) {
            words.push_back(std::move(word));
        }
        return words;
    }

    // The words that all of these names start with, up to a separator: "Canal -" when every name starts
    // with "Canal -". Only a separator ends the cut, so that a name never loses the start of a phrase, as
    // "Road x Street Roundabout" would. Empty when fewer than two names, or when a name has nothing after.
    std::vector<std::string> SharedPrefix(const std::vector<std::string>& names) {
        if (names.size() < 2) {
            return {};
        }
        std::vector<std::vector<std::string>> words;
        for (const std::string& name : names) {
            words.push_back(Words(name));
        }
        size_t prefix = 0;
        while (std::ranges::all_of(words, [&](const auto& name) {
            return prefix < name.size() && name[prefix] == words.front()[prefix];
        })) {
            ++prefix;
        }
        const auto isSeparator = [](const std::string& word) {
            return word == "-" || word == "|" || word.ends_with(':') || word.ends_with(',');
        };
        while (prefix > 0 && !isSeparator(words.front()[prefix - 1])) {
            --prefix;
        }
        if (std::ranges::any_of(words, [prefix](const auto& name) { return name.size() <= prefix; })) {
            return {};
        }
        return {words.front().begin(), words.front().begin() + static_cast<ptrdiff_t>(prefix)};
    }

    // The name without these words in front, when it starts with them and has more: "Canal - straight
    // boat" becomes "Straight boat" without "Canal -". The button or the piece tells those words already.
    std::string WithoutPrefix(const std::string& name, const std::vector<std::string>& prefix) {
        const std::vector<std::string> words = Words(name);
        if (prefix.empty() || words.size() <= prefix.size() || !std::equal(prefix.begin(), prefix.end(), words.begin())) {
            return name;
        }
        std::string shortName = words[prefix.size()];
        for (size_t w = prefix.size() + 1; w < words.size(); ++w) {
            shortName += ' ' + words[w];
        }
        shortName[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(shortName[0])));
        return shortName;
    }

    // "2×3" for the tiles a piece covers, empty for one tile.
    std::string SizeText(const std::vector<std::vector<uint32_t>>& layout) {
        size_t top = SIZE_MAX, bottom = 0, left = SIZE_MAX, right = 0;
        for (size_t row = 0; row < layout.size(); ++row) {
            for (size_t column = 0; column < layout[row].size(); ++column) {
                if (layout[row][column] != 0) {
                    top = std::min(top, row);
                    bottom = std::max(bottom, row);
                    left = std::min(left, column);
                    right = std::max(right, column);
                }
            }
        }
        if (top == SIZE_MAX || (top == bottom && left == right)) {
            return {};
        }
        return std::to_string(right - left + 1) + "\xC3\x97" + std::to_string(bottom - top + 1);
    }

    // Text wrapped at words in up to maxLines lines of this width, the last line cut with an ellipsis when
    // the text is longer. Without a draw list it only measures. Returns the lines it takes and whether it cut.
    std::pair<int, bool> ClampedText(ImDrawList* const drawList, const ImVec2 pos, const float width, const char* text,
                                     const ImU32 color, const int maxLines) {
        ImFont* const font = ImGui::GetFont();
        const float fontSize = ImGui::GetFontSize();
        const float lineHeight = ImGui::GetTextLineHeight();
        const char* const end = text + std::strlen(text);
        int lines = 0;
        while (text < end && lines < maxLines) {
            const char* lineEnd = font->CalcWordWrapPosition(fontSize, text, end, width);
            if (lineEnd == text) {
                lineEnd = text + 1;  // no room for even one character
            }
            const bool cut = lines + 1 == maxLines && lineEnd < end;
            if (drawList) {
                const ImVec2 at(pos.x, pos.y + lines * lineHeight);
                if (cut) {
                    ImGui::PushStyleColor(ImGuiCol_Text, color);
                    ImGui::RenderTextEllipsis(drawList, at, ImVec2(pos.x + width, at.y + lineHeight), pos.x + width,
                                              text, end, nullptr);
                    ImGui::PopStyleColor();
                }
                else {
                    drawList->AddText(at, color, text, lineEnd);
                }
            }
            ++lines;
            if (cut) {
                return {lines, true};
            }
            text = lineEnd;
            while (text < end && *text == ' ') {
                ++text;
            }
        }
        return {lines, false};
    }

    // A row of the button list. Every row is two lines high, so the list looks even: the button's icon when
    // iconSide is not 0, the name on up to two lines, cut with an ellipsis when it is longer, and the count
    // on the right. Hovering a name that is cut shows all of it. icon() gives the icon's texture; it is only
    // asked for while the row is in sight.
    template <typename IconFn>
    bool ButtonRow(const char* name, const size_t count, const bool selected, const float iconSide, IconFn&& icon) {
        char countText[24];
        std::snprintf(countText, sizeof(countText), "%zu", count);
        const float countWidth = ImGui::CalcTextSize(countText).x;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float iconWidth = iconSide > 0.0f ? iconSide + spacing : 0.0f;
        const float nameWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x - iconWidth - countWidth - spacing);
        const float lineHeight = ImGui::GetTextLineHeight();

        const bool clicked = ImGui::Selectable("##button", selected, ImGuiSelectableFlags_None,
                                               ImVec2(0.0f, lineHeight * 2.0f));
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        if (iconSide > 0.0f && ImGui::IsItemVisible()) {
            if (const std::optional<void*> texture = icon(); texture && *texture) {
                const float iconTop = min.y + (max.y - min.y - iconSide) * 0.5f;
                drawList->AddImage(reinterpret_cast<ImTextureID>(*texture), ImVec2(min.x, iconTop),
                                   ImVec2(min.x + iconSide, iconTop + iconSide));
            }
        }
        const auto [lines, cut] = ClampedText(nullptr, min, nameWidth, name, 0, 2);
        const float top = min.y + (max.y - min.y - lines * lineHeight) * 0.5f;
        ClampedText(drawList, ImVec2(min.x + iconWidth, top), nameWidth, name, ImGui::GetColorU32(ImGuiCol_Text), 2);
        drawList->AddText(ImVec2(max.x - countWidth, min.y + (max.y - min.y - lineHeight) * 0.5f),
                          ImGui::GetColorU32(ImGuiCol_TextDisabled), countText);
        if (cut && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", name);
        }
        return clicked;
    }

    // The networks in the cells of a piece: bit n is eNetworkType n.
    uint32_t PieceNetworks(const std::vector<std::vector<uint32_t>>& layout) {
        uint32_t networks = 0;
        for (const auto& row : layout) {
            for (const uint32_t cell : row) {
                networks |= cell & ((1u << kBaseNetworkNames.size()) - 1);
            }
        }
        return networks;
    }
}

NetworksPanelTab::NetworksPanelTab(SC4PlopAndPaintDirector* director, FavoritesRepository* favorites,
                                   cIGZImGuiService* imguiService)
    : PanelTab(director, nullptr, nullptr, favorites, imguiService) {
    gridView_ = true;  // tiles only, no list
    gridTileScale_ = 1.4f;
    // A step up from the other tabs: on a smaller tile the size and row badges cover the picture.
    tileSizes_[0] = 66.0f;
    tileSizes_[1] = 88.0f;
    tileSizes_[2] = 132.0f;
    gridTileSize_ = tileSizes_[1];
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
    uint32_t allNetworks = 0;
    for (NetworkPieceButton& button : cache_.buttons) {
        const size_t b = &button - cache_.buttons.data();
        if (button.path.empty()) {
            button.path.emplace_back("Other");
        }
        if (std::ranges::find(menus_, button.path.front()) == menus_.end()) {
            menus_.push_back(button.path.front());
        }
        std::string where = button.name + ' ' + button.label;
        for (const std::string& menu : button.path) {
            where += ' ' + menu;
        }
        std::vector<std::string> pieceNames;
        for (const NetworkPiece& piece : button.pieces) {
            pieceNames.push_back(piece.name);
        }
        const std::vector<std::string> buttonWords = SharedPrefix(pieceNames);

        auto& buttonVariants = variants_.emplace_back();
        for (size_t p = 0; p < button.pieces.size(); ++p) {
            const NetworkPiece& piece = button.pieces[p];
            std::vector<std::string> names{piece.name};
            for (const NetworkPieceVariant& variant : piece.variants) {
                names.push_back(variant.name);
            }
            const std::vector<std::string> pieceWords = SharedPrefix(names);
            auto& pieceVariants = buttonVariants.emplace_back();
            // The cache builder's label is the tile's name. The search goes by the full name and the label, so
            // that "OWR" also finds "One-Way Road".
            const auto add = [&](const std::string& name, const std::string& label, const uint32_t id,
                                 const uint32_t cost, const std::vector<std::vector<uint32_t>>& layout,
                                 const std::vector<uint32_t>& entries) {
                const size_t v = pieceVariants.size();
                Variant& variant = pieceVariants.emplace_back();
                variant.name = name;
                variant.shortName = label.empty() ? WithoutPrefix(name, buttonWords) : label;
                variant.pickerName = WithoutPrefix(name, pieceWords);
                variant.id = id;
                variant.cost = cost;
                variant.networks = PieceNetworks(layout);
                variant.size = SizeText(layout);
                variant.search = TextSearch::Normalize(name + ' ' + label + ' ' + where) + TextSearch::HexId(id);
                variant.nameSearch = TextSearch::Normalize(name + ' ' + label) + TextSearch::HexId(id);
                suggester_.Add(variant.search);
                allNetworks |= variant.networks;
                pieceById_.try_emplace(id, Tile{{b, p}, v});
                for (const uint32_t entry : entries) {
                    pieceById_.try_emplace(entry, Tile{{b, p}, v});
                }
            };
            add(piece.name, piece.label, piece.id, piece.cost, piece.layout, piece.entries);
            for (const NetworkPieceVariant& variant : piece.variants) {
                add(variant.name, variant.label, variant.id, variant.cost, variant.layout, variant.entries);
            }
        }
        pieceCount_ += button.pieces.size();
    }

    // The Network Eraser has its own button above the views, so it is not in the button list.
    for (size_t b = 0; b < cache_.buttons.size(); ++b) {
        const NetworkPieceButton& button = cache_.buttons[b];
        if (button.pieces.front().id == kNetworkEraserPiece ||
            (eraserButton_ == kNone && button.name == "Network Eraser")) {
            eraserButton_ = b;
        }
    }
    // The button list keeps the order of the game's menus, as the cache has it: the NAM puts buttons that
    // work alike next to each other.
    for (size_t b = 0; b < cache_.buttons.size(); ++b) {
        if (b != eraserButton_) {
            buttonOrder_.push_back(b);
        }
    }
    if (!buttonOrder_.empty()) {
        selectedButton_ = buttonOrder_.front();
    }
    // The networks to filter by: those of the game's menus in their order, then the others.
    for (const BaseNetwork& network : kBaseNetworks) {
        if (allNetworks & (1u << network.type)) {
            networkChoices_.push_back(network.type);
        }
    }
    for (uint32_t type = 0; type < kBaseNetworkNames.size(); ++type) {
        if ((allNetworks & (1u << type)) && std::ranges::find(networkChoices_, type) == networkChoices_.end()) {
            networkChoices_.push_back(type);
        }
    }
    // Icons only count when the thumbnail file has them: a cache from an older builder has none.
    for (const BaseNetworkTool& tool : cache_.baseTools) {
        if (tool.network < baseIcons_.size() && thumbnails_.HasThumbnail(IconKey(tool.icon))) {
            baseIcons_[tool.network] = tool.icon;
        }
    }
    buttonIcons_ = std::ranges::any_of(buttonOrder_, [this](const size_t b) {
        return cache_.buttons[b].icon != 0 && thumbnails_.HasThumbnail(IconKey(cache_.buttons[b].icon));
    });
    LOG_INFO("NetworksPanelTab: {} override networks, {} menu buttons with pieces", cache_.networks.size(),
             cache_.buttons.size());
}

void NetworksPanelTab::OnDeviceReset(const uint32_t deviceGeneration) {
    if (deviceGeneration != lastDeviceGeneration_) {
        thumbnailCache_.OnDeviceReset();
        iconCache_.OnDeviceReset();
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

std::optional<void*> NetworksPanelTab::Icon_(const uint32_t icon) {
    if (icon == 0) {
        return std::nullopt;
    }
    const uint64_t key = IconKey(icon);
    ++visibleIcons_;
    const auto texture = iconCache_.Get(key);
    if (!texture && thumbnails_.HasThumbnail(key)) {
        iconCache_.Request(key);
    }
    return texture;
}

float NetworksPanelTab::BaseNetworkButtonWidth_(const char* name, const uint32_t icon) const {
    if (icon == 0) {
        return ButtonWidth(name);
    }
    return IconSide_() + ImGui::CalcTextSize(name).x + ImGui::GetStyle().FramePadding.x * 3.0f;
}

bool NetworksPanelTab::RenderBaseNetworkButton_(const char* name, const uint32_t icon, const bool active) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    bool clicked = false;
    if (icon == 0) {
        clicked = ImGui::Button(name);
    }
    else {
        // The icon on the left, inset a little so that the button's frame shows around it, and the name.
        const float side = IconSide_();
        const float inset = 3.0f;
        ImGui::PushID(name);
        clicked = ImGui::Button("##base", ImVec2(BaseNetworkButtonWidth_(name, icon), side + inset * 2.0f));
        ImGui::PopID();
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        if (const auto texture = Icon_(icon); texture && *texture) {
            drawList->AddImage(reinterpret_cast<ImTextureID>(*texture), ImVec2(min.x + inset, min.y + inset),
                               ImVec2(min.x + inset + side, min.y + inset + side));
        }
        const float textLeft = min.x + inset + side + ImGui::GetStyle().FramePadding.x;
        drawList->AddText(ImVec2(textLeft, min.y + (max.y - min.y - ImGui::GetTextLineHeight()) * 0.5f),
                          ImGui::GetColorU32(ImGuiCol_Text), name);
    }
    if (active) {
        ImGui::PopStyleColor();
    }
    return clicked;
}

bool NetworksPanelTab::IsFavorite_(const uint64_t favoriteKey) const {
    return favorites_ && favorites_->IsFamilyFavorite(favoriteKey);
}

void NetworksPanelTab::RenderTileMenu_(const uint64_t favoriteKey) const {
    if (favorites_ && favoriteKey != 0 && ImGui::BeginPopupContextItem("##tileMenu")) {
        if (ImGui::Selectable(IsFavorite_(favoriteKey) ? "Remove from favorites" : "Add to favorites")) {
            favorites_->ToggleFamilyFavorite(favoriteKey);
        }
        ImGui::EndPopup();
    }
}

float NetworksPanelTab::RenderTileFace_(const uint64_t key, const std::string& name, const char* badge,
                                        const bool active, const uint64_t favoriteKey) {
    const auto texture = thumbnailCache_.Get(key);
    if (!texture && thumbnails_.HasThumbnail(key)) {
        thumbnailCache_.Request(key);
    }
    // The name on a dark band at the foot of the tile, and the picture in the square above it, so that the
    // name never covers the model. Pictures of network pieces look much alike, so the name gets two lines
    // from the default tile size on: one line holds about 11 letters, too few to tell most pieces apart.
    // Small tiles have room for one line only.
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float fontSize = ImGui::GetFontSize() * 0.8f;
    const float lines = max.y - min.y >= ImGui::GetFontSize() * 4.5f ? 2.0f : 1.0f;
    const float bandTop = max.y - (fontSize * lines + 6.0f);
    RenderGridTile_(std::nullopt);  // the background and border of the whole tile
    if (texture && *texture) {
        const float side = std::min(bandTop - min.y, max.x - min.x);
        const ImVec2 pictureMin(min.x + (max.x - min.x - side) * 0.5f, min.y);
        drawList->AddImage(reinterpret_cast<ImTextureID>(*texture), pictureMin,
                           ImVec2(pictureMin.x + side, pictureMin.y + side));
    }
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
    ThumbnailGrid::DecorateLastTile(active, favoriteKey != 0 && IsFavorite_(favoriteKey));
    RenderTileMenu_(favoriteKey);
    return bandTop;
}

void NetworksPanelTab::RenderNetworkTile_(const OverrideNetwork& network, const bool active) {
    // A network whose models do not render has the picture of its starter piece, which falls back to a
    // drawing of its cells.
    const uint64_t pictureKey = thumbnails_.HasThumbnail(NetworkKey(network)) ? NetworkKey(network)
                                                                              : MakeGIKey(1, network.starterPiece);
    char wide[16] = {};
    if (network.rows.size() > 1) {
        std::snprintf(wide, sizeof(wide), "x%zu", network.rows.size());
    }
    RenderTileFace_(pictureKey, network.name, wide[0] ? wide : nullptr, active, NetworkFavorite(network));
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
        RenderLargePreview_(thumbnails_, pictureKey);
        ImGui::EndTooltip();
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        ReleaseImGuiInputCapture_();
        director_->StartOverrideNetworkDrag(network);
    }
}

void NetworksPanelTab::RenderPieceTile_(const Tile tile, const bool active, const bool shortName) {
    const auto [b, p] = tile.ref;
    const NetworkPieceButton& button = cache_.buttons[b];
    const NetworkPiece& piece = button.pieces[p];
    const std::vector<Variant>& variants = variants_[b][p];
    const Variant& shown = variants[tile.variant];
    const float bandTop = RenderTileFace_(VariantKey(shown.id), shortName ? shown.shortName : shown.name,
                                          shown.size.empty() ? nullptr : shown.size.c_str(), active,
                                          PieceFavorite(piece));
    const bool tileHovered = ImGui::IsItemHovered();
    const bool tileClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

    // A row with more pieces than this one has a badge in the corner above the name: two stacked squares
    // and how many pieces there are. A click on it opens the list of them.
    bool overBadge = false;
    if (variants.size() > 1) {
        char count[16];
        std::snprintf(count, sizeof(count), "%zu", variants.size());
        const float textHeight = ImGui::GetFontSize();
        const float square = std::floor(textHeight * 0.55f);
        const ImVec2 countSize = ImGui::CalcTextSize(count);
        const ImVec2 max(ImGui::GetItemRectMax().x - 3.0f, bandTop - 3.0f);
        const ImVec2 min(max.x - (square + 3.0f + 4.0f + countSize.x + 10.0f), max.y - (textHeight + 4.0f));
        overBadge = tileHovered && ImGui::IsMouseHoveringRect(min, max);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(min, max, overBadge ? IM_COL32(80, 80, 80, 240) : IM_COL32(32, 32, 32, 220), 3.0f);
        if (overBadge) {
            drawList->AddRect(min, max, IM_COL32(255, 255, 255, 160), 3.0f);
        }
        const ImU32 ink = IM_COL32(220, 220, 220, 255);
        const ImVec2 front(min.x + 5.0f, min.y + (max.y - min.y - square) * 0.5f + 1.5f);
        drawList->AddRect(ImVec2(front.x + 3.0f, front.y - 3.0f), ImVec2(front.x + 3.0f + square, front.y - 3.0f + square),
                          ink);
        drawList->AddRectFilled(front, ImVec2(front.x + square, front.y + square), IM_COL32(32, 32, 32, 255));
        drawList->AddRect(front, ImVec2(front.x + square, front.y + square), ink);
        drawList->AddText(ImVec2(front.x + square + 3.0f + 4.0f, min.y + 2.0f), ink, count);
    }

    if (overBadge) {
        ImGui::SetTooltip("%zu pieces in this row. Click to choose one.", variants.size());
    }
    else if (tileHovered) {
        ImGui::BeginTooltip();
        // As wide as the picture: the name wraps, and where the piece is stops at two lines.
        const float width = thumbnails_.StoredSize() > 0 ? static_cast<float>(thumbnails_.StoredSize())
                                                         : ImGui::GetFontSize() * 18.0f;
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextUnformatted(shown.name.c_str());
        ImGui::PopTextWrapPos();
        const std::string where = button.path.front() + ", " + ButtonLabel(button);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const auto [lines, cut] = ClampedText(ImGui::GetWindowDrawList(), at, width, where.c_str(),
                                              ImGui::GetColorU32(ImGuiCol_TextDisabled), 2);
        ImGui::Dummy(ImVec2(width, lines * ImGui::GetTextLineHeight()));
        if (!shown.size.empty()) {
            ImGui::TextDisabled("%s tiles", shown.size.c_str());
        }
        if (shown.cost > 0) {
            ImGui::TextDisabled("Cost: \xC2\xA7%u", shown.cost);  // the simoleon sign, U+00A7
        }
        if (variants.size() > 1) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
            ImGui::TextDisabled("One of %zu pieces in this row. Click the badge to choose another, or press Home "
                                "or End in the city.",
                                variants.size());
            ImGui::PopTextWrapPos();
        }
        RenderLargePreview_(thumbnails_, VariantKey(shown.id));
        ImGui::EndTooltip();
    }

    if (tileClicked) {
        if (overBadge) {
            ImGui::OpenPopup("##variants");
        }
        else {
            PlacePiece_(tile);
        }
    }
    if (variants.size() > 1) {
        RenderVariantPicker_(tile.ref, active ? std::optional(tile.variant) : std::nullopt);
    }
}

void NetworksPanelTab::RenderVariantPicker_(const PieceRef ref, const std::optional<size_t> active) {
    const std::vector<Variant>& variants = variants_[ref.first][ref.second];
    const float tileSize = TileSize_();
    const ImGuiStyle& style = ImGui::GetStyle();
    const auto columns = static_cast<float>(std::min<size_t>(variants.size(), 4));
    // Room for up to four tiles in a row, and a pixel more so that rounding never drops one.
    ImGui::SetNextWindowSize(ImVec2(
        columns * tileSize + (columns - 1.0f) * style.ItemSpacing.x + 2.0f * style.WindowPadding.x + 1.0f, 0.0f));
    if (!ImGui::BeginPopup("##variants")) {
        return;
    }
    ImGui::TextUnformatted("Pieces in this row");
    visibleTiles_ += ThumbnailGrid::Render(
        variants.size(), tileSize, [&](const size_t v) { return VariantKey(variants[v].id); },
        [&](const size_t v) {
            const Variant& variant = variants[v];
            RenderTileFace_(VariantKey(variant.id), variant.pickerName, variant.size.empty() ? nullptr : variant.size.c_str(),
                            active == v, 0);
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                const float width = thumbnails_.StoredSize() > 0 ? static_cast<float>(thumbnails_.StoredSize())
                                                                 : ImGui::GetFontSize() * 18.0f;
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
                ImGui::TextUnformatted(variant.name.c_str());
                ImGui::PopTextWrapPos();
                if (variant.cost > 0) {
                    ImGui::TextDisabled("Cost: \xC2\xA7%u", variant.cost);
                }
                RenderLargePreview_(thumbnails_, VariantKey(variant.id));
                ImGui::EndTooltip();
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                PlacePiece_({ref, v});
                ImGui::CloseCurrentPopup();
            }
        });
    PanelUi::SecondaryText("Home and End in the city go through these too, and turn them.");
    ImGui::EndPopup();
}

void NetworksPanelTab::PlacePiece_(const Tile tile) {
    ReleaseImGuiInputCapture_();
    const NetworkPieceButton& button = cache_.buttons[tile.ref.first];
    const NetworkPiece& piece = button.pieces[tile.ref.second];
    if (director_->StartNetworkPiece(button.id, piece.id, variants_[tile.ref.first][tile.ref.second][tile.variant].id)) {
        // Recent has each piece once, as the variant used last.
        std::erase_if(recent_, [&](const Tile& used) { return used.ref == tile.ref; });
        recent_.insert(recent_.begin(), tile);
        if (recent_.size() > kMaxRecent) {
            recent_.pop_back();
        }
    }
}

bool NetworksPanelTab::VariantFiltered_(const Variant& variant) const {
    return (variant.networks & networkFilter_) == networkFilter_;
}

bool NetworksPanelTab::PieceFiltered_(const PieceRef ref) const {
    return (!favoritesOnly_ || IsFavorite_(PieceFavorite(cache_.buttons[ref.first].pieces[ref.second]))) &&
           std::ranges::any_of(variants_[ref.first][ref.second],
                               [this](const Variant& variant) { return VariantFiltered_(variant); });
}

bool NetworksPanelTab::PieceShown_(const PieceRef ref, const TextSearch::Query& query) const {
    return (!favoritesOnly_ || IsFavorite_(PieceFavorite(cache_.buttons[ref.first].pieces[ref.second]))) &&
           std::ranges::any_of(variants_[ref.first][ref.second], [&](const Variant& variant) {
               return VariantFiltered_(variant) && query.Matches(variant.search);
           });
}

bool NetworksPanelTab::PieceNamed_(const PieceRef ref, const TextSearch::Query& query) const {
    return !query.Empty() && std::ranges::any_of(variants_[ref.first][ref.second], [&](const Variant& variant) {
               return VariantFiltered_(variant) && query.Matches(variant.nameSearch);
           });
}

NetworksPanelTab::Tile NetworksPanelTab::TileOf_(const PieceRef ref, const TextSearch::Query& query) const {
    const std::vector<Variant>& variants = variants_[ref.first][ref.second];
    if (!query.Empty()) {
        for (size_t v = 0; v < variants.size(); ++v) {
            if (VariantFiltered_(variants[v]) && query.Matches(variants[v].nameSearch)) {
                return {ref, v};
            }
        }
    }
    for (size_t v = 0; v < variants.size(); ++v) {
        if (VariantFiltered_(variants[v]) && query.Matches(variants[v].search)) {
            return {ref, v};
        }
    }
    return {ref, 0};
}

std::optional<NetworksPanelTab::Tile> NetworksPanelTab::ActiveTile_() const {
    if (const auto found = pieceById_.find(director_->ActiveNetworkPiece()); found != pieceById_.end()) {
        return found->second;
    }
    return std::nullopt;
}

int NetworksPanelTab::ActiveFilterCount_() const {
    return static_cast<int>(searchBuf_[0] != '\0') + static_cast<int>(favoritesOnly_) +
           static_cast<int>(view_ == View::Pieces && menu_ >= 0) +
           static_cast<int>(view_ == View::Pieces && networkFilter_ != 0);
}

void NetworksPanelTab::ClearFilters_() {
    searchBuf_[0] = '\0';
    favoritesOnly_ = false;
    menu_ = -1;
    networkFilter_ = 0;
}

std::string NetworksPanelTab::NetworkFilterLabel_() const {
    std::string label;
    for (const uint32_t type : networkChoices_) {
        if (networkFilter_ & (1u << type)) {
            label += (label.empty() ? "" : " + ") + std::string(NetworkTypeName(type));
        }
    }
    return label.empty() ? "All networks" : label;
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
    if (view_ == View::Pieces && !networkChoices_.empty()) {
        NextInRow(index, UI::dropdownWidth());
        ImGui::SetNextItemWidth(UI::dropdownWidth());
        if (ImGui::BeginCombo("##Networks", NetworkFilterLabel_().c_str())) {
            for (const uint32_t type : networkChoices_) {
                bool on = (networkFilter_ & (1u << type)) != 0;
                if (ImGui::Checkbox(NetworkTypeName(type), &on)) {
                    networkFilter_ ^= 1u << type;
                }
            }
            ImGui::EndCombo();
        }
        Tooltip("Show only the pieces that have all the networks that you select. For example, select Avenue "
                "and Rail to find the pieces where they cross.");
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
    if (const auto tile = ActiveTile_()) {
        return "Placing " + variants_[tile->ref.first][tile->ref.second][tile->variant].name;
    }
    return {};
}

void NetworksPanelTab::RenderEraserButton_() {
    if (eraserButton_ == kNone) {
        return;
    }
    const PieceRef eraser{eraserButton_, 0};
    const std::optional<Tile> active = ActiveTile_();
    const bool erasing = active && active->ref == eraser;
    if (erasing) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    if (ImGui::SmallButton("Network eraser")) {
        PlacePiece_({eraser, 0});
    }
    if (erasing) {
        ImGui::PopStyleColor();
    }
    Tooltip("Erase one tile of a draggable or FLEX network: click the tile in the city.");
    ImGui::SameLine();
}

void NetworksPanelTab::RenderToolRow_() {
    // The eraser is at hand in both views, whatever the filters and the selected button.
    RenderEraserButton_();
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
    // The plain networks first. They have no picture, so they are buttons that wrap, with the icon of
    // their button in the game's menus.
    if (!bases.empty() && ImGui::CollapsingHeader("Base networks", ImGuiTreeNodeFlags_DefaultOpen)) {
        const int activeBase = director_->ActiveBaseNetwork();
        for (size_t i = 0; i < bases.size(); ++i) {
            const BaseNetwork& network = kBaseNetworks[bases[i]];
            const uint32_t icon = baseIcons_[network.type];
            NextInRow(i, BaseNetworkButtonWidth_(network.name, icon));
            if (RenderBaseNetworkButton_(network.name, icon, activeBase == static_cast<int>(network.type))) {
                ReleaseImGuiInputCapture_();
                director_->StartBaseNetworkDrag(network.type);
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
                                         const std::vector<std::vector<size_t>>& pieces, const size_t recentCount,
                                         const bool collapsible) {
    // The icon column is there for all rows, so that the names line up. Recent has no icon.
    const float iconSide = buttonIcons_ ? IconSide_() : 0.0f;
    if (recentCount > 0) {
        ImGui::PushID("Recent");
        if (ButtonRow("Recent", recentCount, selectedButton_ == kRecent, iconSide,
                      [] { return std::optional<void*>(); })) {
            selectedButton_ = kRecent;
        }
        ImGui::PopID();
    }
    bool open = true;
    for (size_t i = 0; i < buttons.size(); ++i) {
        const NetworkPieceButton& button = cache_.buttons[buttons[i]];
        // The menu, and the submenus below it when there are some, as a heading above its buttons. The
        // headings start closed, but for the one of the selected button, which also opens when TAB in the
        // city goes to a button under a closed heading.
        if (i == 0 || button.path != cache_.buttons[buttons[i - 1]].path) {
            std::string heading = button.path.front();
            for (size_t level = 1; level < button.path.size(); ++level) {
                heading += " / " + button.path[level];
            }
            if (collapsible) {
                if (openSelectedMenu_ && selectedButton_ != kRecent &&
                    cache_.buttons[selectedButton_].path == button.path) {
                    ImGui::SetNextItemOpen(true);
                    openSelectedMenu_ = false;
                }
                open = ImGui::CollapsingHeader((heading + "##menu").c_str());
            }
            else {
                ImGui::SeparatorText(heading.c_str());
                open = true;
            }
        }
        if (!open) {
            continue;
        }
        ImGui::PushID(static_cast<int>(buttons[i]));
        if (ButtonRow(ButtonLabel(button).c_str(), pieces[i].size(), selectedButton_ == buttons[i], iconSide,
                      [&] { return Icon_(button.icon); })) {
            selectedButton_ = buttons[i];
            scrollToButton_ = buttons[i];
        }
        ImGui::PopID();
    }
}

void NetworksPanelTab::RenderPiecesView_(const TextSearch::Query& query) {
    if (cache_.buttons.empty()) {
        Hint("No pieces found. Run the cache builder again to list the pieces of the network menus.");
        return;
    }

    // The buttons that are in the chosen menu and have a piece that the filters let through. When some
    // pieces of a button match by their own name, only those count: the others only match through the
    // name of their button or menu.
    std::vector<size_t> buttons;
    std::vector<std::vector<size_t>> pieces;
    size_t count = 0;
    for (const size_t b : buttonOrder_) {
        const NetworkPieceButton& button = cache_.buttons[b];
        if (menu_ >= 0 && button.path.front() != menus_[menu_]) {
            continue;
        }
        std::vector<size_t> matches;
        std::vector<size_t> named;
        for (size_t p = 0; p < button.pieces.size(); ++p) {
            if (PieceShown_({b, p}, query)) {
                matches.push_back(p);
                if (PieceNamed_({b, p}, query)) {
                    named.push_back(p);
                }
            }
        }
        if (!named.empty()) {
            matches = std::move(named);
        }
        if (!matches.empty()) {
            count += matches.size();
            buttons.push_back(b);
            pieces.push_back(std::move(matches));
        }
    }
    // Recent shows each piece as the variant used last, while the filters let that variant through.
    std::vector<Tile> recent;
    for (const Tile& tile : recent_) {
        const PieceRef ref = tile.ref;
        if ((menu_ < 0 || cache_.buttons[ref.first].path.front() == menus_[menu_]) && PieceShown_(ref, query)) {
            const Variant& used = variants_[ref.first][ref.second][tile.variant];
            recent.push_back(VariantFiltered_(used) && query.Matches(used.search) ? tile : TileOf_(ref, query));
        }
    }

    RenderResultLine_(count, pieceCount_, "pieces");
    RenderToolRow_();
    if (count == 0) {
        RenderNoResults_("No pieces match these filters.");
        return;
    }

    // TAB, Home or End in the game changed the piece, or a click here did: go to its tile.
    const std::optional<Tile> activeTile = ActiveTile_();
    if (const uint32_t activePiece = director_->ActiveNetworkPiece(); activePiece != lastActivePiece_) {
        lastActivePiece_ = activePiece;
        if (activeTile) {
            reveal_ = activeTile->ref;
            if (selectedButton_ != kRecent && std::ranges::find(buttons, activeTile->ref.first) != buttons.end()) {
                openSelectedMenu_ = openSelectedMenu_ || selectedButton_ != activeTile->ref.first;
                selectedButton_ = activeTile->ref.first;
            }
        }
    }

    auto selected = std::ranges::find(buttons, selectedButton_);
    if (selectedButton_ == kRecent ? recent.empty() : selected == buttons.end()) {
        selectedButton_ = buttons.front();
        selected = buttons.begin();
        openSelectedMenu_ = true;
    }

    // The list keeps at least half of a narrow panel for the pieces. Drag its edge to make it wider.
    const float listWidth = std::min(ImGui::GetFontSize() * 15.0f, ImGui::GetContentRegionAvail().x * 0.45f);
    if (ImGui::BeginChild("Buttons", ImVec2(listWidth, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX)) {
        RenderButtonList_(buttons, pieces, recent.size(), query.Empty() && networkFilter_ == 0);
    }
    ImGui::EndChild();
    ImGui::SameLine();

    if (ImGui::BeginChild("Pieces", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        const auto tilesOf = [&](const size_t index) {
            std::vector<Tile> tiles;
            for (const size_t p : pieces[index]) {
                tiles.push_back(TileOf_({buttons[index], p}, query));
            }
            return tiles;
        };
        if (selectedButton_ == kRecent) {
            RenderPieceGrid_(recent, activeTile, false);
        }
        else if (!query.Empty() || networkFilter_ != 0) {
            // A search or a network filter: the matches of all buttons, each under its button. A click
            // on a button in the list goes to its pieces. The pieces whose own name matches come first, and
            // the rest of their TAB loop follows greyed out. When only the button's name matches, its pieces
            // all show as they are.
            for (size_t i = 0; i < buttons.size(); ++i) {
                if (buttons[i] == scrollToButton_) {
                    ImGui::SetScrollHereY(0.0f);
                }
                ImGui::SeparatorText(ButtonLabel(cache_.buttons[buttons[i]]).c_str());
                ImGui::PushID(static_cast<int>(buttons[i]));
                std::vector<Tile> named;
                std::vector<Tile> rest;
                if (!query.Empty()) {
                    for (size_t p = 0; p < cache_.buttons[buttons[i]].pieces.size(); ++p) {
                        const PieceRef ref{buttons[i], p};
                        if (PieceFiltered_(ref)) {
                            (PieceNamed_(ref, query) ? named : rest).push_back(TileOf_(ref, query));
                        }
                    }
                }
                if (named.empty()) {
                    RenderPieceGrid_(tilesOf(i), activeTile, true);
                }
                else {
                    const size_t bright = named.size();
                    named.insert(named.end(), rest.begin(), rest.end());
                    RenderPieceGrid_(named, activeTile, true, bright);
                }
                ImGui::PopID();
            }
        }
        else {
            RenderPieceGrid_(tilesOf(selected - buttons.begin()), activeTile, true);
        }
        scrollToButton_ = kNone;
        reveal_.reset();  // drawn now, or not shown with these filters
    }
    ImGui::EndChild();
}

void NetworksPanelTab::RenderPieceGrid_(const std::vector<Tile>& tiles, const std::optional<Tile> active,
                                        const bool shortNames, const size_t brightCount) {
    const float tileSize = TileSize_();
    if (reveal_) {
        // Scroll only when the tile is not in sight already.
        if (const auto at = std::ranges::find(tiles, *reveal_, &Tile::ref); at != tiles.end()) {
            const float top = ImGui::GetCursorPosY() + ThumbnailGrid::ScrollYFor(at - tiles.begin(), tileSize);
            if (top < ImGui::GetScrollY() || top + tileSize > ImGui::GetScrollY() + ImGui::GetWindowHeight()) {
                ImGui::SetScrollY(top);
            }
            reveal_.reset();
        }
    }
    // The tile's ID is that of the piece, whichever variant it shows, so that its list of variants stays
    // open when Home or End change the variant.
    visibleTiles_ += ThumbnailGrid::Render(
        tiles.size(), tileSize,
        [&](const size_t i) { return PieceKey(cache_.buttons[tiles[i].ref.first].pieces[tiles[i].ref.second]); },
        [&](const size_t i) {
            // The piece being placed shows the variant that Home and End went to.
            const bool isActive = active && active->ref == tiles[i].ref;
            RenderPieceTile_(isActive ? *active : tiles[i], isActive, shortNames);
            if (i >= brightCount) {
                ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                          ImGui::GetColorU32(ImGuiCol_WindowBg, 0.65f),
                                                          ThumbnailUi::kCornerRounding);
            }
        });
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
    visibleIcons_ = 0;
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
    const float iconSide = IconSide_();
    iconCache_.FitVisible(visibleIcons_ + Cache::kMaxLoadPerFrame);
    iconCache_.ProcessLoadQueue([this, iconSide](const uint64_t key) { return LoadTexture_(key, iconSide); });
}
