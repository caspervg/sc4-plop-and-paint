#include "NetworkDiscovery.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <spdlog/spdlog.h>

#include "DbpfIndexService.hpp"
#include "LTextReader.h"
#include "RUL0.h"
#include "RUL1.h"

namespace {
    constexpr uint32_t kRulType = 0x0A5BCF4B;
    constexpr uint32_t kRulGroup = 0xAA5BCF57;
    constexpr uint32_t kLTextType = 0x2026960B;
    constexpr uint32_t kPlaceQueryGroup = 0x2A592FD1;
    constexpr uint32_t kExemplarType = 0x6534284A;
    constexpr uint32_t kMenuButtonGroup = 0x2A3858E4;
    constexpr uint32_t kMenuButtonInstance = 0x6A470000;  // plus the first piece ID of the button's TAB loop
    constexpr uint32_t kUserVisibleNameKey = 0x8A416A99;
    constexpr uint32_t kExemplarName = 0x00000020;
    constexpr uint32_t kItemOrder = 0x8A2602B9;
    constexpr uint32_t kItemButtonId = 0x8A2602BB;
    constexpr uint32_t kItemSubmenuParentId = 0x8A2602CA;  // from the Submenus DLL
    constexpr uint32_t kEffDirType = 0xEA5118B0;
    constexpr uint32_t kModelType = 0x5AD0E817;
    constexpr uint32_t kPieceModelGroup = 0xBADB57F1;

    // The game's network menus: the ID of the menu's own button, which a submenu names as its parent, and
    // the property a button has when it is in that menu directly.
    struct TopMenu {
        uint32_t button;
        uint32_t key;
        const char* name;
    };
    constexpr TopMenu kTopMenus[] = {
        {0x6999BF56, 0x4000, "Roads"},    {0x00000031, 0x4005, "Highways"},
        {0x00000029, 0x4001, "Rail"},     {0x299237BF, 0x4002, "Miscellaneous Transit"},
        {0xE99234B3, 0x4007, "Airports"}, {0xA99234A6, 0x4006, "Water Transit"},
    };

    // The plain orthogonal straight tile of each base network. These are the game's own tiles (RHW-2
    // for the dirt road), so they do not change with the NAM version.
    // ponytail: single-tile base networks only. Avenue and the highways are two tiles wide themselves.
    const std::unordered_map<uint32_t, RUL0::NetworkType> kBaseTiles = {
        {0x00004B00, RUL0::NetworkType::ROAD},
        {0x5D540000, RUL0::NetworkType::RAIL},  // as a rail drag solves it, before RUL2
        {0x05004B00, RUL0::NetworkType::STREET},
        {0x08031500, RUL0::NetworkType::LIGHT_RAIL},
        {0x09004B00, RUL0::NetworkType::ONE_WAY_ROAD},
        {0x0D031500, RUL0::NetworkType::MONORAIL},
        {0x57000000, RUL0::NetworkType::DIRT_ROAD},
    };

    // The plain diagonal tile of the same base networks, as in the NAM DLL's diagonal surrogate tiles.
    const std::unordered_map<uint32_t, RUL0::NetworkType> kBaseDiagonalTiles = {
        {0x00000A00, RUL0::NetworkType::ROAD},
        {0x5D540100, RUL0::NetworkType::RAIL},
        {0x5F500200, RUL0::NetworkType::STREET},
        {0x08001A00, RUL0::NetworkType::LIGHT_RAIL},
        {0x09000A00, RUL0::NetworkType::ONE_WAY_ROAD},
        {0x0D001A00, RUL0::NetworkType::MONORAIL},
        {0x57000200, RUL0::NetworkType::DIRT_ROAD},
    };

    // RUL1 names its sections after the two networks, in this spelling.
    constexpr std::array<std::string_view, 13> kRul1Names = {
        "road", "rail", "highway", "street", "pipe", "powerline", "avenue",
        "subway", "lightrail", "monorail", "onewayroad", "dirtroad", "groundhighway"};

    using SpreadingTiles = std::unordered_map<uint32_t, RUL0::NetworkType>;
    // Tiles that do not spread themselves but turn into one that does: RUL2 rules "T,r,f,B,r,f=X,r,f,X,r,f"
    // where B is a base tile and X spreads over it. A drag then turns into X. The STR starter does this.
    using DragTiles = std::unordered_map<uint32_t, uint32_t>;
    // Tiles that change the plain diagonal of a base network next to them, with a bit per base network
    // (1 << NetworkType).
    using DiagonalLeads = std::unordered_map<uint32_t, uint32_t>;
    struct Rul2Facts {
        SpreadingTiles spreading;
        DiagonalLeads diagonalLeads;
        DragTiles dragTiles;
        // Spreading tiles that do not repeat themselves but lead into a tile that does, such as a transition
        // in front of a network. Which tile they lead into.
        std::unordered_map<uint32_t, uint32_t> leadsInto;
    };
    using Rul1Sections = std::unordered_map<std::string, const RUL1::Section*>;

    std::string Lower(std::string_view text) {
        std::string result(text);
        std::ranges::transform(result, result.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return result;
    }

    // Tiles that spread over a base network, with that network: RUL2 rules "S,r,f,B,r,f=S,r,f,N,r,f"
    // where B is a base tile, and following N the same way comes back round. Mostly N is S itself, but
    // fractional angles go through a cycle of tiles (#1.5: A, B, A, ...), some after a first tile (#3: A, B, B). And tiles that change a base diagonal tile D next to them: "S,r,f,D,r,f=S,r,f,X,r,f"
    // or the other way round. That is how the NAM DLL starts a direct drag on a diagonal.
    // The file has millions of rules, so this reads the four IDs of a line and nothing else.
    Rul2Facts ReadRul2(const std::vector<uint8_t>& rul2) {
        Rul2Facts result;
        std::unordered_map<uint32_t, std::pair<uint32_t, RUL0::NetworkType>> turnsInto;
        std::unordered_map<uint32_t, std::pair<uint32_t, RUL0::NetworkType>> next;  // the N of S, above
        const char* line = reinterpret_cast<const char*>(rul2.data());
        const char* const end = line + rul2.size();
        while (line < end) {
            const char* const lineEnd = std::find(line, end, '\n');
            if (*line == '0') {
                std::array<uint32_t, 4> ids{};
                size_t count = 0;
                for (const char* p = line; count < ids.size() && p + 2 < lineEnd && *p != ';';) {
                    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
                        const auto parsed = std::from_chars(p + 2, lineEnd, ids[count], 16);
                        if (parsed.ec != std::errc{}) {
                            break;
                        }
                        ++count;
                        p = parsed.ptr;
                    }
                    else {
                        ++p;
                    }
                }
                if (count == ids.size() && ids[2] == ids[3] && ids[0] != ids[1]) {
                    if (const auto base = kBaseTiles.find(ids[1]); base != kBaseTiles.end()) {
                        if (ids[0] == ids[2]) {
                            result.spreading.try_emplace(ids[0], base->second);
                        }
                        else {
                            turnsInto.try_emplace(ids[0], ids[2], base->second);
                        }
                    }
                }
                if (count == ids.size() && ids[0] == ids[2] && ids[0] != ids[3] && ids[1] != ids[3]) {
                    if (const auto base = kBaseTiles.find(ids[1]); base != kBaseTiles.end()) {
                        next.try_emplace(ids[0], ids[3], base->second);
                    }
                }
                if (count == ids.size()) {
                    if (const auto diagonal = kBaseDiagonalTiles.find(ids[1]);
                        diagonal != kBaseDiagonalTiles.end() && ids[0] != ids[1] && ids[3] != ids[1]) {
                        result.diagonalLeads[ids[0]] |= 1u << static_cast<uint32_t>(diagonal->second);
                    }
                    if (const auto diagonal = kBaseDiagonalTiles.find(ids[0]);
                        diagonal != kBaseDiagonalTiles.end() && ids[1] != ids[0] && ids[2] != ids[0]) {
                        result.diagonalLeads[ids[1]] |= 1u << static_cast<uint32_t>(diagonal->second);
                    }
                }
            }
            line = lineEnd == end ? end : lineEnd + 1;
        }
        // A cycle: from S, the tiles that follow come back to one of them. Cycles are short, a few tiles.
        for (const auto& [start, first] : next) {
            uint32_t tile = start;
            std::vector<uint32_t> seen;
            for (int step = 0; step < 8 && !result.spreading.contains(start); ++step) {
                seen.push_back(tile);
                const auto following = next.find(tile);
                if (following == next.end() || following->second.second != first.second) {
                    break;
                }
                tile = following->second.first;
                if (std::ranges::find(seen, tile) != seen.end()) {
                    result.spreading.emplace(start, first.second);
                }
                else if (result.spreading.contains(tile)) {
                    result.spreading.emplace(start, first.second);
                    result.leadsInto.emplace(start, tile);
                }
            }
        }
        for (const auto& [tile, into] : turnsInto) {
            if (const auto spreads = result.spreading.find(into.first);
                !result.spreading.contains(tile) && spreads != result.spreading.end() && spreads->second == into.second) {
                result.dragTiles.emplace(tile, into.first);
            }
        }
        for (const auto& [tile, into] : result.dragTiles) {
            result.spreading.emplace(tile, result.spreading.at(into));
        }
        return result;
    }

    // What RUL1 lays where two networks cross with these edge flags, or null.
    const RUL1::Rule* SolveCrossing(const Rul1Sections& sections, const RUL0::NetworkCheck& a, const RUL0::NetworkCheck& b) {
        const auto ia = static_cast<size_t>(a.networkType);
        const auto ib = static_cast<size_t>(b.networkType);
        if (ia >= kRul1Names.size() || ib >= kRul1Names.size()) {
            return nullptr;
        }
        const auto find = [&](const size_t first, const uint32_t firstEdges, const size_t second,
                              const uint32_t secondEdges) -> const RUL1::Rule* {
            const auto section = sections.find(std::string(kRul1Names[first]) + std::string(kRul1Names[second]));
            if (section == sections.end()) {
                return nullptr;
            }
            for (const auto& rule : section->second->rules) {
                if (rule.firstEdges == firstEdges && rule.secondEdges == secondEdges) {
                    return &rule;
                }
            }
            return nullptr;
        };
        const auto* rule = find(ia, a.ruleFlagByte, ib, b.ruleFlagByte);
        return rule ? rule : find(ib, b.ruleFlagByte, ia, a.ruleFlagByte);
    }

    // The piece with its layout, following CopyFrom for the rotated copies.
    const RUL0::PuzzlePiece* ResolvePiece(const RUL0::Record& rul0, uint32_t id) {
        for (int depth = 0; depth < 16; ++depth) {
            const auto piece = rul0.puzzlePieces.find(id);
            if (piece == rul0.puzzlePieces.end()) {
                return nullptr;
            }
            if (!piece->second.checkTypes.empty() || piece->second.copyFrom == 0) {
                return &piece->second;
            }
            id = piece->second.copyFrom;
        }
        return nullptr;
    }

    // A tile's rotation and flip after the whole piece turns by a number of quarter turns. Rotation 0
    // heads south and each step turns clockwise: south, west, north, east. A flipped tile counts
    // its rotation the other way round.
    constexpr uint32_t Turn(const uint32_t rotFlip, const uint32_t quarterTurns) {
        return (rotFlip + (1 | (rotFlip >> 6)) * (quarterTurns & 3)) & 0x83;
    }
    static_assert(Turn(0x00, 1) == 0x01 && Turn(0x03, 2) == 0x01 && Turn(0x80, 1) == 0x83 && Turn(0x82, 0) == 0x82);

    struct Starter {
        RUL0::NetworkType base;
        std::vector<OverrideNetworkRow> rows;
    };

    // A starter piece is nothing but its rows side by side, plus at most one plain tile of the base
    // network that shows where to drag. Other pieces that happen to hold a network's tile (ramps,
    // transitions) have more cells and are not starters.
    std::optional<Starter> StarterRows(const RUL0::PuzzlePiece& piece, const Rul1Sections& sections,
                                       const SpreadingTiles& spreading, const DragTiles& dragTiles) {
        struct Cell {
            int32_t x, z;
            const RUL1::Rule* rule;
        };
        std::vector<Cell> cells;
        size_t letters = 0;
        for (size_t z = 0; z < piece.cellLayout.size(); ++z) {
            const auto& line = piece.cellLayout[z];
            for (size_t x = 0; x < line.size(); ++x) {
                const char symbol = line[x];
                if (symbol == '.' || symbol == '<' || symbol == '^' || symbol == ' ' || symbol == '\t') {
                    continue;
                }
                ++letters;
                const auto check = std::ranges::find(piece.checkTypes, symbol, &RUL0::CheckType::symbol);
                if (check == piece.checkTypes.end()) {
                    return std::nullopt;
                }
                if (check->networks.size() == 2) {
                    const auto* rule = SolveCrossing(sections, check->networks[0], check->networks[1]);
                    if (rule && spreading.contains(rule->textureId)) {
                        cells.push_back({static_cast<int32_t>(x), static_cast<int32_t>(z), rule});
                    }
                }
            }
        }
        if (cells.empty() || letters - cells.size() > 1) {
            return std::nullopt;
        }

        // Turn the piece so that the middle row heads south (rotation 0). The cursor follows that row.
        const Cell& middle = cells[cells.size() / 2];
        const uint32_t turns = (4 - middle.rule->rotation) & 3;
        Starter starter{spreading.at(middle.rule->textureId), {}};
        for (const Cell& cell : cells) {
            int32_t x = cell.x - middle.x;
            int32_t z = cell.z - middle.z;
            for (uint32_t i = 0; i < turns; ++i) {
                std::tie(x, z) = std::pair{-z, x};  // a quarter turn clockwise, as tile rotations count
            }
            if (z != 0 || spreading.at(cell.rule->textureId) != starter.base) {
                return std::nullopt;  // not side by side
            }
            const uint32_t rotFlip = (cell.rule->rotation & 3u) | (cell.rule->flip ? 0x80u : 0u);
            // The rule that turns a starter tile into its drag tile keeps the orientation.
            const auto drag = dragTiles.find(cell.rule->textureId);
            starter.rows.push_back({drag != dragTiles.end() ? drag->second : cell.rule->textureId, x, Turn(rotFlip, turns)});
        }
        std::ranges::sort(starter.rows, {}, &OverrideNetworkRow::offset);
        for (size_t i = 1; i < starter.rows.size(); ++i) {
            if (starter.rows[i].offset != starter.rows[i - 1].offset + 1) {
                return std::nullopt;
            }
        }
        return starter;
    }

    std::string FirstLine(std::string text) {
        text.erase(std::min(text.find_first_of("\r\n"), text.size()));
        // Some names are padded with runs of spaces.
        text.erase(std::unique(text.begin(), text.end(), [](const char a, const char b) { return a == ' ' && b == ' '; }),
                   text.end());
        while (!text.empty() && text.back() == ' ') {
            text.pop_back();
        }
        return text;
    }

    std::string LoadText(const DbpfIndexService& index, const DBPF::Tgi& tgi) {
        if (const auto data = index.loadEntryData(tgi)) {
            if (const auto text = LText::Parse(std::span(data->data(), data->size())); text.has_value()) {
                return FirstLine(text->ToUtf8());  // the lines after the first are usage notes
            }
        }
        return {};
    }

    std::string PieceName(const DbpfIndexService& index, const RUL0::PuzzlePiece& piece) {
        std::string name = LoadText(index, DBPF::Tgi{kLTextType, kPlaceQueryGroup, piece.placeQueryId});
        for (const std::string_view word : {" Starter Piece", " Starter"}) {
            if (const auto at = name.find(word); at != std::string::npos) {
                name.erase(at, word.size());
            }
        }
        // "STR - Starter Piece - Orth" leaves a dash with nothing after it.
        for (size_t at; (at = name.find(" - -")) != std::string::npos;) {
            name.erase(at, 2);
        }
        if (name.ends_with(" -")) {
            name.erase(name.size() - 2);
        }
        return name.empty() ? fmt::format("Piece 0x{:X}", piece.id) : name;
    }

    // The model of every preview effect, by the effect's name in lower case. The game shows this model
    // on the cursor. For one piece in six it is not the model of the Piece line: it can have marks that
    // the real piece does not have, such as arrows for the direction of travel, and for some pieces the
    // Piece line names no model at all.
    //
    // ponytail: this reads the NAM's own EFFDIR entries only. They all have one layout: two particles,
    // the first for the effect and the second for its red twin, with the model key of the first at 0xC8.
    // The game's big EFFDIR has another layout and needs a full reader (the format is in
    // sc4-effects-extensions, docs/reference/binary/effdir.md). The few Maxis pieces fall back to the
    // Piece line.
    std::unordered_map<std::string, uint32_t> LoadPreviewModels(const DbpfIndexService& index) {
        constexpr std::array<uint8_t, 8> kLayout = {0x04, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00};
        constexpr size_t kModelKeyOffset = 0xC8;
        constexpr std::string_view kPrefix = "preview";
        std::unordered_map<std::string, uint32_t> models;
        for (const DBPF::Tgi& tgi : index.typeIndex(kEffDirType)) {
            const auto data = index.loadEntryData(tgi);
            if (!data || data->size() < kModelKeyOffset + 4 || !std::equal(kLayout.begin(), kLayout.end(), data->begin())) {
                continue;
            }
            const auto u32At = [&data](const size_t at) {
                return static_cast<uint32_t>((*data)[at]) | (*data)[at + 1] << 8 | (*data)[at + 2] << 16 |
                       static_cast<uint32_t>((*data)[at + 3]) << 24;
            };
            const uint32_t model = u32At(kModelKeyOffset);
            // The effect names are strings with their length in front.
            const std::string_view text(reinterpret_cast<const char*>(data->data()), data->size());
            for (size_t at = text.find(kPrefix); at != std::string_view::npos; at = text.find(kPrefix, at + 1)) {
                if (at < 4) {
                    continue;
                }
                const size_t length = u32At(at - 4);
                if (length < kPrefix.size() || length > 200 || at + length > text.size()) {
                    continue;
                }
                std::string name = Lower(text.substr(at, length));
                if (!name.ends_with("_red")) {
                    models.insert_or_assign(std::move(name), model);
                }
            }
        }
        return models;
    }

    // The cells of a piece as NetworkPiece::layout has them, cut to the cells that are in use. Two kinds of
    // cell in the CellLayout get no tile of the piece, so they are not cells of the piece either:
    // - a stub, whose CheckType says "check": it connects the piece to a drag next to it.
    // - the placeholder of a FLEX piece: the one cell that is not optional, which the NAM DLL removes before
    //   the piece is placed (FlexPieces.cpp). The optional cells around it make the piece.
    std::vector<std::vector<uint32_t>> PieceLayout(const RUL0::PuzzlePiece& piece) {
        constexpr uint32_t kFlexAutoTileBase = 0x55387000;
        std::vector<std::vector<uint32_t>> cells;
        std::vector<std::pair<size_t, size_t>> staticCells;  // row, column of the cells that are not optional
        size_t cellCount = 0;
        for (const auto& line : piece.cellLayout) {
            auto& row = cells.emplace_back();
            for (const char symbol : line) {
                uint32_t cell = 0;
                if (symbol == '<' || symbol == '^' || symbol == ' ' || symbol == '\t') {
                    // '<' and '^' mark the row and column of the cursor, they are not cells
                }
                else if (symbol != '.') {
                    cell = NetworkPiece::kOtherCell;
                    bool isStatic = symbol == '+';
                    if (const auto check = std::ranges::find(piece.checkTypes, symbol, &RUL0::CheckType::symbol);
                        check != piece.checkTypes.end()) {
                        // "check" and "optional" are for the whole line: every network of it has the same.
                        if (std::ranges::any_of(check->networks, &RUL0::NetworkCheck::check)) {
                            row.push_back(0);
                            continue;
                        }
                        isStatic = isStatic || std::ranges::none_of(check->networks, &RUL0::NetworkCheck::optional);
                        for (const auto& network : check->networks) {
                            if (const auto type = static_cast<uint32_t>(network.networkType); type < kRul1Names.size()) {
                                cell = (cell & ~NetworkPiece::kOtherCell) | (1u << type);
                            }
                        }
                    }
                    if (isStatic) {
                        staticCells.emplace_back(cells.size() - 1, row.size());
                    }
                    ++cellCount;
                }
                row.push_back(cell);
            }
        }
        if (piece.autoTileBase == kFlexAutoTileBase && staticCells.size() == 1 && cellCount > 1) {
            cells[staticCells.front().first][staticCells.front().second] = 0;
        }

        size_t left = SIZE_MAX, right = 0, top = SIZE_MAX, bottom = 0;
        for (size_t z = 0; z < cells.size(); ++z) {
            for (size_t x = 0; x < cells[z].size(); ++x) {
                if (cells[z][x] != 0) {
                    left = std::min(left, x);
                    right = std::max(right, x);
                    top = std::min(top, z);
                    bottom = z;
                }
            }
        }
        if (left == SIZE_MAX) {
            return {};
        }
        std::vector<std::vector<uint32_t>> layout;
        for (size_t z = top; z <= bottom; ++z) {
            auto& row = layout.emplace_back(right - left + 1, 0u);
            for (size_t x = left; x <= right && x < cells[z].size(); ++x) {
                row[x - left] = cells[z][x];
            }
        }
        return layout;
    }

    // The models of a network tile come in five, one for each zoom, with the zoom in the last digit of the
    // ID (0 for the farthest, 4 for the closest). The Piece line often names the farthest. The closest has
    // the largest textures, so that is the one for a picture.
    uint32_t ClosestZoomModel(const DbpfIndexService& index, const uint32_t model) {
        const uint32_t closest = (model & ~0xFu) | 4;
        return (model & 0xF) < 4 && index.containsTgi(DBPF::Tgi{kModelType, kPieceModelGroup, closest}) ? closest : model;
    }

    // A button of the game's menus: a network item or a submenu.
    struct MenuItem {
        std::string name;
        uint32_t parent = 0;  // the button that opens the submenu this one is in, 0 when there is none
        uint32_t order = 0;
        int topMenu = -1;     // index in kTopMenus when the button is in that menu directly
    };

    std::unordered_map<uint32_t, MenuItem> LoadMenuItems(const DbpfIndexService& index) {
        std::unordered_map<uint32_t, MenuItem> items;
        for (const DBPF::Tgi& tgi : index.typeIndex(kExemplarType)) {
            if (tgi.group != kMenuButtonGroup) {
                continue;
            }
            const auto exemplar = index.loadExemplar(tgi);
            if (!exemplar.has_value() || !*exemplar) {
                continue;
            }
            const Exemplar::Record& record = **exemplar;
            const auto* id = record.FindProperty(kItemButtonId);
            if (!id || id->values.empty()) {
                continue;
            }
            MenuItem item;
            if (const auto* key = record.FindProperty(kUserVisibleNameKey); key && key->values.size() >= 3) {
                item.name = LoadText(index, DBPF::Tgi{kLTextType, key->GetScalarAs<uint32_t>(1).value_or(0),
                                                      key->GetScalarAs<uint32_t>(2).value_or(0)});
            }
            if (const auto* name = record.FindProperty(kExemplarName); item.name.empty() && name) {
                item.name = name->GetScalarAs<std::string>().value_or("");
            }
            if (const auto* parent = record.FindProperty(kItemSubmenuParentId)) {
                item.parent = parent->GetScalarAs<uint32_t>().value_or(0);
            }
            if (const auto* order = record.FindProperty(kItemOrder)) {
                item.order = order->GetScalarAs<uint32_t>().value_or(0);
            }
            for (int menu = 0; menu < static_cast<int>(std::size(kTopMenus)); ++menu) {
                if (record.FindProperty(kTopMenus[menu].key)) {
                    item.topMenu = menu;
                }
            }
            items.try_emplace(id->GetScalarAs<uint32_t>().value_or(0), std::move(item));
        }
        return items;
    }

    // Where a button is: the menu, then the submenus down to it. The second value puts the buttons in
    // the order of the game's menus.
    std::pair<std::vector<std::string>, std::vector<uint32_t>> MenuPath(
        const std::unordered_map<uint32_t, MenuItem>& items, const MenuItem& button) {
        std::vector<std::string> names;
        std::vector<uint32_t> orders{button.order};
        int topMenu = button.topMenu;
        uint32_t parent = button.parent;
        for (int depth = 0; parent != 0 && depth < 8; ++depth) {
            if (const auto top = std::ranges::find(kTopMenus, parent, &TopMenu::button); top != std::end(kTopMenus)) {
                topMenu = static_cast<int>(top - std::begin(kTopMenus));
                break;
            }
            const auto submenu = items.find(parent);
            if (submenu == items.end()) {
                break;
            }
            names.push_back(submenu->second.name);
            orders.push_back(submenu->second.order);
            parent = submenu->second.parent;
        }
        names.emplace_back(topMenu >= 0 ? kTopMenus[topMenu].name : "Other");
        orders.push_back(topMenu >= 0 ? static_cast<uint32_t>(topMenu) : static_cast<uint32_t>(std::size(kTopMenus)));
        std::ranges::reverse(names);
        std::ranges::reverse(orders);
        return {std::move(names), std::move(orders)};
    }
}

NetworksCache NetworkDiscovery::Discover(const DbpfIndexService& index) {
    const auto rul0Data = index.loadEntryData(DBPF::Tgi{kRulType, kRulGroup, 0x10000000});
    const auto rul1Data = index.loadEntryData(DBPF::Tgi{kRulType, kRulGroup, 0x10000001});
    const auto rul2Data = index.loadEntryData(DBPF::Tgi{kRulType, kRulGroup, 0x10000002});
    if (!rul0Data || !rul1Data || !rul2Data) {
        spdlog::info("No network pieces: the plugins have no RUL0, RUL1 or RUL2");
        return {};
    }
    const auto rul0 = RUL0::Parse(std::span(rul0Data->data(), rul0Data->size()));
    const auto rul1 = RUL1::Parse(std::span(rul1Data->data(), rul1Data->size()));
    if (!rul0.has_value() || !rul1.has_value()) {
        spdlog::warn("No network pieces: could not read RUL0 or RUL1");
        return {};
    }

    const auto rul2Facts = ReadRul2(*rul2Data);
    const auto& spreading = rul2Facts.spreading;
    Rul1Sections sections;
    for (const auto& section : rul1->sections) {
        sections.try_emplace(Lower(section.pairName), &section);
    }
    const auto menuItems = LoadMenuItems(index);
    const auto previewModels = LoadPreviewModels(index);

    NetworksCache cache;
    std::map<std::vector<std::pair<uint32_t, uint32_t>>, size_t> seen;  // rows -> index in networks
    std::vector<std::pair<std::vector<uint32_t>, NetworkPieceButton>> buttons;  // with the menu order
    // The TAB loops in the order of RUL0. That is the order the NAM team chose, with the levels of a
    // network together.
    for (const auto& ordering : rul0->orderings) {
        if (ordering.rotationRing.empty()) {
            continue;
        }
        const auto menuItem = menuItems.find(kMenuButtonInstance | (ordering.rotationRing.front() & 0xFFFF));
        const std::string group = menuItem != menuItems.end() ? menuItem->second.name : std::string();
        NetworkPieceButton button{group, {}, ordering.rotationRing.front(), {}};

        const auto consider = [&](const std::vector<uint32_t>& row) {
            const auto* piece = row.empty() ? nullptr : ResolvePiece(*rul0, row.front());
            if (!piece) {
                return;
            }
            // The picture shows the real model, the one of the Piece line, when there is one. The model
            // of the preview effect is second choice.
            uint32_t model = piece->effect.initialized ? piece->effect.instanceId : 0;
            uint32_t fallback = 0;
            if (const auto preview = previewModels.find(Lower(piece->effect.name));
                preview != previewModels.end() && preview->second != model) {
                fallback = preview->second;
                if (!index.containsTgi(DBPF::Tgi{kModelType, kPieceModelGroup, model})) {
                    std::swap(model, fallback);
                    fallback = 0;
                }
            }
            model = ClosestZoomModel(index, model);
            std::string name = LoadText(index, DBPF::Tgi{kLTextType, kPlaceQueryGroup, piece->placeQueryId});
            button.pieces.push_back({name.empty() ? fmt::format("Piece 0x{:X}", row.front()) : std::move(name),
                                     row.front(), model, fallback, static_cast<uint32_t>(row.size()),
                                     PieceLayout(*piece), piece->costs != 0xFFFFFFFF ? piece->costs : 0});

            // A starter piece is also an override network that can be dragged directly.
            auto starter = StarterRows(*piece, sections, spreading, rul2Facts.dragTiles);
            if (!starter) {
                return;
            }
            // A tile that only leads into a network is mostly a transition, such as a FLEX turn lane, which
            // is placed, not dragged. Only those that NAM calls a starter are networks (Fractional Angle #3).
            const bool leadIn = std::ranges::any_of(starter->rows, [&](const OverrideNetworkRow& row) {
                return rul2Facts.leadsInto.contains(row.tile);
            });
            if (leadIn && LoadText(index, DBPF::Tgi{kLTextType, kPlaceQueryGroup, piece->placeQueryId}).find("Starter") ==
                              std::string::npos) {
                return;
            }
            std::vector<std::pair<uint32_t, uint32_t>> key;
            for (const auto& r : starter->rows) {
                key.emplace_back(r.tile, r.rotFlip);
            }
            if (seen.try_emplace(std::move(key), cache.networks.size()).second) {
                // ponytail: one lead-in rule counts as diagonal support. A rule that only makes an end piece
                // of the diagonal also counts. Simulate the drag (rul2-sim) if that gives wrong marks.
                // The NAM DLL drags the other rows of a wide network straight only.
                bool diagonal = false;
                if (starter->rows.size() == 1) {
                    const auto leads = rul2Facts.diagonalLeads.find(starter->rows.front().tile);
                    diagonal = leads != rul2Facts.diagonalLeads.end() &&
                               (leads->second >> static_cast<uint32_t>(starter->base) & 1) != 0;
                }
                cache.networks.push_back({PieceName(index, *piece), group, static_cast<uint32_t>(starter->base),
                                          piece->id, model, fallback, std::move(starter->rows), diagonal});
            }
        };
        consider(ordering.rotationRing);
        for (const auto& row : ordering.addTypes) {
            consider(row);
        }

        // A TAB loop without a menu button cannot be reached in the game, so it is not listed either.
        if (menuItem != menuItems.end() && !button.pieces.empty()) {
            auto [path, order] = MenuPath(menuItems, menuItem->second);
            button.path = std::move(path);
            buttons.emplace_back(std::move(order), std::move(button));
        }
    }

    // The Piece line must name a model, but any model will do, so some pieces name a stand-in: the
    // underground road starters all name the same eraser. A model that pieces with different preview
    // effects share is such a stand-in, and the preview model is the one to show.
    std::unordered_map<uint32_t, uint32_t> previewOfModel;
    std::unordered_set<uint32_t> standIns;
    for (const auto& [order, button] : buttons) {
        for (const NetworkPiece& piece : button.pieces) {
            if (piece.fallbackModel == 0) {
                continue;
            }
            if (const auto [seen, isNew] = previewOfModel.try_emplace(piece.previewModel, piece.fallbackModel);
                !isNew && seen->second != piece.fallbackModel) {
                standIns.insert(piece.previewModel);
            }
        }
    }
    const auto preferPreview = [&standIns](uint32_t& model, uint32_t& fallback) {
        if (fallback != 0 && standIns.contains(model)) {
            std::swap(model, fallback);
        }
    };
    for (auto& [order, button] : buttons) {
        for (NetworkPiece& piece : button.pieces) {
            preferPreview(piece.previewModel, piece.fallbackModel);
        }
    }
    for (OverrideNetwork& network : cache.networks) {
        preferPreview(network.previewModel, network.fallbackModel);
    }

    // A starter that only leads into the drag tile of another network, such as an on-slope transition in
    // front of a viaduct, drags that network. It is not a network of its own.
    std::unordered_set<uint32_t> dragTiles;
    for (const auto& network : cache.networks) {
        for (const auto& row : network.rows) {
            dragTiles.insert(row.tile);
        }
    }
    std::erase_if(cache.networks, [&](const OverrideNetwork& network) {
        return std::ranges::any_of(network.rows, [&](const OverrideNetworkRow& row) {
            const auto into = rul2Facts.leadsInto.find(row.tile);
            return into != rul2Facts.leadsInto.end() && dragTiles.contains(into->second);
        });
    });

    std::ranges::stable_sort(buttons, {}, [](const auto& entry) { return entry.first; });
    size_t pieces = 0;
    for (auto& [order, button] : buttons) {
        pieces += button.pieces.size();
        cache.buttons.push_back(std::move(button));
    }
    spdlog::info("Found {} override networks ({} drag on diagonals) and {} pieces under {} menu buttons",
                 cache.networks.size(), std::ranges::count(cache.networks, true, &OverrideNetwork::diagonal), pieces,
                 cache.buttons.size());
    return cache;
}

std::vector<std::byte> NetworkDiscovery::LayoutPicture(const NetworkPiece& piece, const uint32_t size) {
    // A colour for each network, by cISC4NetworkOccupant::eNetworkType, then one for other cells.
    constexpr std::array<std::array<uint8_t, 3>, 14> kColors = {{
        {120, 120, 120}, {135, 100, 65},  {70, 72, 90},    {170, 170, 165}, {60, 120, 200},
        {220, 200, 60},  {95, 95, 95},    {150, 90, 170},  {90, 140, 175},  {60, 160, 150},
        {150, 145, 115}, {48, 48, 54},    {85, 88, 110},   {110, 135, 110},
    }};
    size_t columns = 0;
    for (const auto& row : piece.layout) {
        columns = std::max(columns, row.size());
    }
    if (columns == 0 || size < 16) {
        return {};
    }
    const uint32_t margin = size / 12;
    const uint32_t cellSize = std::max<uint32_t>(2, (size - 2 * margin) / static_cast<uint32_t>(std::max(columns, piece.layout.size())));
    const uint32_t left = (size - cellSize * static_cast<uint32_t>(columns)) / 2;
    const uint32_t top = (size - cellSize * static_cast<uint32_t>(piece.layout.size())) / 2;
    const uint32_t gap = cellSize >= 8 ? 1 : 0;

    std::vector<std::byte> pixels(static_cast<size_t>(size) * size * 4, std::byte{0});
    for (size_t z = 0; z < piece.layout.size(); ++z) {
        for (size_t x = 0; x < piece.layout[z].size(); ++x) {
            const uint32_t cell = piece.layout[z][x];
            if (cell == 0) {
                continue;
            }
            // The networks in the cell, at most two: the first takes the upper half, the second the lower.
            std::array<size_t, 2> networks = {13, 13};
            size_t count = 0;
            for (size_t type = 0; type < 13 && count < networks.size(); ++type) {
                if (cell & (1u << type)) {
                    networks[count++] = type;
                }
            }
            if (count < 2) {
                networks[1] = networks[0];
            }
            for (uint32_t dz = gap; dz + gap < cellSize; ++dz) {
                const auto& color = kColors[networks[dz * 2 < cellSize ? 0 : 1]];
                for (uint32_t dx = gap; dx + gap < cellSize; ++dx) {
                    const size_t at = (static_cast<size_t>(top + static_cast<uint32_t>(z) * cellSize + dz) * size +
                                       left + static_cast<uint32_t>(x) * cellSize + dx) * 4;
                    pixels[at] = std::byte{color[0]};
                    pixels[at + 1] = std::byte{color[1]};
                    pixels[at + 2] = std::byte{color[2]};
                    pixels[at + 3] = std::byte{255};
                }
            }
        }
    }
    return pixels;
}
