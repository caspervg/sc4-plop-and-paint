#include "NetworkDiscovery.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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

    // The plain orthogonal straight tile of each base network. These are the game's own tiles (RHW-2
    // for the dirt road), so they do not change with the NAM version.
    // ponytail: single-tile base networks only. Avenue and the highways are two tiles wide themselves.
    const std::unordered_map<uint32_t, RUL0::NetworkType> kBaseTiles = {
        {0x00004B00, RUL0::NetworkType::ROAD},
        {0x03031500, RUL0::NetworkType::RAIL},
        {0x05004B00, RUL0::NetworkType::STREET},
        {0x08031500, RUL0::NetworkType::LIGHT_RAIL},
        {0x09004B00, RUL0::NetworkType::ONE_WAY_ROAD},
        {0x0D031500, RUL0::NetworkType::MONORAIL},
        {0x57000000, RUL0::NetworkType::DIRT_ROAD},
    };

    // RUL1 names its sections after the two networks, in this spelling.
    constexpr std::array<std::string_view, 13> kRul1Names = {
        "road", "rail", "highway", "street", "pipe", "powerline", "avenue",
        "subway", "lightrail", "monorail", "onewayroad", "dirtroad", "groundhighway"};

    using SpreadingTiles = std::unordered_map<uint32_t, RUL0::NetworkType>;
    using Rul1Sections = std::unordered_map<std::string, const RUL1::Section*>;

    std::string Lower(std::string_view text) {
        std::string result(text);
        std::ranges::transform(result, result.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return result;
    }

    // Tiles that spread over a base network, with that network: RUL2 rules "S,r,f,B,r,f=S,r,f,S,r,f"
    // where B is a base tile. The file has millions of rules, so this reads the four IDs of a line and
    // nothing else.
    SpreadingTiles FindSpreadingTiles(const std::vector<uint8_t>& rul2) {
        SpreadingTiles result;
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
                if (count == ids.size() && ids[0] == ids[2] && ids[0] == ids[3] && ids[0] != ids[1]) {
                    if (const auto base = kBaseTiles.find(ids[1]); base != kBaseTiles.end()) {
                        result.try_emplace(ids[0], base->second);
                    }
                }
            }
            line = lineEnd == end ? end : lineEnd + 1;
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
                                       const SpreadingTiles& spreading) {
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
            starter.rows.push_back({cell.rule->textureId, x, Turn(rotFlip, turns)});
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
        return name.empty() ? fmt::format("Piece 0x{:X}", piece.id) : name;
    }

    // The name of the menu button that opens this TAB loop.
    std::string ButtonName(const DbpfIndexService& index, const uint32_t firstPiece) {
        const auto exemplar = index.loadExemplar(
            DBPF::Tgi{kExemplarType, kMenuButtonGroup, kMenuButtonInstance | (firstPiece & 0xFFFF)});
        if (!exemplar.has_value() || !*exemplar) {
            return {};
        }
        if (const auto* key = (*exemplar)->FindProperty(kUserVisibleNameKey); key && key->values.size() >= 3) {
            const DBPF::Tgi tgi{kLTextType, key->GetScalarAs<uint32_t>(1).value_or(0), key->GetScalarAs<uint32_t>(2).value_or(0)};
            if (auto name = LoadText(index, tgi); !name.empty()) {
                return name;
            }
        }
        if (const auto* name = (*exemplar)->FindProperty(kExemplarName)) {
            return name->GetScalarAs<std::string>().value_or("");
        }
        return {};
    }
}

std::vector<OverrideNetwork> NetworkDiscovery::FindOverrideNetworks(const DbpfIndexService& index) {
    const auto rul0Data = index.loadEntryData(DBPF::Tgi{kRulType, kRulGroup, 0x10000000});
    const auto rul1Data = index.loadEntryData(DBPF::Tgi{kRulType, kRulGroup, 0x10000001});
    const auto rul2Data = index.loadEntryData(DBPF::Tgi{kRulType, kRulGroup, 0x10000002});
    if (!rul0Data || !rul1Data || !rul2Data) {
        spdlog::info("No override networks: the plugins have no RUL0, RUL1 or RUL2");
        return {};
    }
    const auto rul0 = RUL0::Parse(std::span(rul0Data->data(), rul0Data->size()));
    const auto rul1 = RUL1::Parse(std::span(rul1Data->data(), rul1Data->size()));
    if (!rul0.has_value() || !rul1.has_value()) {
        spdlog::warn("No override networks: could not read RUL0 or RUL1");
        return {};
    }

    const auto spreading = FindSpreadingTiles(*rul2Data);
    Rul1Sections sections;
    for (const auto& section : rul1->sections) {
        sections.try_emplace(Lower(section.pairName), &section);
    }

    // The first piece of every row of a TAB loop is a candidate, in the order of the TAB loops. That is
    // the order the NAM team chose, with the levels of a network together.
    std::vector<OverrideNetwork> networks;
    std::map<std::vector<std::pair<uint32_t, uint32_t>>, size_t> seen;  // rows -> index in networks
    for (const auto& ordering : rul0->orderings) {
        if (ordering.rotationRing.empty()) {
            continue;
        }
        std::optional<std::string> group;
        const auto consider = [&](const std::vector<uint32_t>& row) {
            const auto* piece = row.empty() ? nullptr : ResolvePiece(*rul0, row.front());
            auto starter = piece ? StarterRows(*piece, sections, spreading) : std::nullopt;
            if (!starter) {
                return;
            }
            std::vector<std::pair<uint32_t, uint32_t>> key;
            for (const auto& r : starter->rows) {
                key.emplace_back(r.tile, r.rotFlip);
            }
            if (!seen.try_emplace(std::move(key), networks.size()).second) {
                return;
            }
            if (!group) {
                group = ButtonName(index, ordering.rotationRing.front());
            }
            networks.push_back({PieceName(index, *piece), *group, static_cast<uint32_t>(starter->base), piece->id,
                                piece->effect.initialized ? piece->effect.instanceId : 0, std::move(starter->rows)});
        };
        consider(ordering.rotationRing);
        for (const auto& row : ordering.addTypes) {
            consider(row);
        }
    }
    spdlog::info("Found {} override networks", networks.size());
    return networks;
}
