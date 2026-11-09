#include "NetworkDiscovery.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>

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

    // The plain orthogonal straight tile of each base network. These are the game's own tiles (RHW-2
    // for the dirt road), so they do not change with the NAM version.
    // ponytail: single-tile base networks only. Avenue and the highways need two rows per drag.
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

    std::string Lower(std::string_view text) {
        std::string result(text);
        std::ranges::transform(result, result.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return result;
    }

    // Tiles that spread over a base network, with that network: RUL2 rules "S,r,f,B,r,f=S,r,f,S,r,f"
    // where B is a base tile. The file has millions of rules, so this reads the four IDs of a line and
    // nothing else.
    std::unordered_map<uint32_t, RUL0::NetworkType> FindSpreadingTiles(const std::vector<uint8_t>& rul2) {
        std::unordered_map<uint32_t, RUL0::NetworkType> result;
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

    // The tile RUL1 gives for two networks crossing with these edge flags, or 0.
    uint32_t SolveCrossing(const std::unordered_map<std::string, const RUL1::Section*>& sections,
                           const RUL0::NetworkCheck& a, const RUL0::NetworkCheck& b) {
        const auto ia = static_cast<size_t>(a.networkType);
        const auto ib = static_cast<size_t>(b.networkType);
        if (ia >= kRul1Names.size() || ib >= kRul1Names.size()) {
            return 0;
        }
        const auto find = [&](const size_t first, const uint32_t firstEdges, const size_t second,
                              const uint32_t secondEdges) -> uint32_t {
            const auto section = sections.find(std::string(kRul1Names[first]) + std::string(kRul1Names[second]));
            if (section == sections.end()) {
                return 0;
            }
            for (const auto& rule : section->second->rules) {
                if (rule.firstEdges == firstEdges && rule.secondEdges == secondEdges) {
                    return rule.textureId;
                }
            }
            return 0;
        };
        const uint32_t tile = find(ia, a.ruleFlagByte, ib, b.ruleFlagByte);
        return tile != 0 ? tile : find(ib, b.ruleFlagByte, ia, a.ruleFlagByte);
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

    std::string PieceName(const DbpfIndexService& index, const RUL0::PuzzlePiece& piece) {
        std::string name;
        if (const auto data = index.loadEntryData(DBPF::Tgi{kLTextType, kPlaceQueryGroup, piece.placeQueryId})) {
            if (const auto text = LText::Parse(std::span(data->data(), data->size())); text.has_value()) {
                name = text->ToUtf8();
            }
        }
        name.erase(std::min(name.find_first_of("\r\n"), name.size()));  // the lines after the first are usage notes
        for (const std::string_view word : {" Starter Piece", " Starter"}) {
            if (const auto at = name.find(word); at != std::string::npos) {
                name.erase(at, word.size());
            }
        }
        // Some names are padded with runs of spaces.
        name.erase(std::unique(name.begin(), name.end(), [](const char a, const char b) { return a == ' ' && b == ' '; }),
                   name.end());
        while (!name.empty() && name.back() == ' ') {
            name.pop_back();
        }
        return name.empty() ? fmt::format("Piece 0x{:X}", piece.id) : name;
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
    std::unordered_map<std::string, const RUL1::Section*> sections;
    for (const auto& section : rul1->sections) {
        sections.try_emplace(Lower(section.pairName), &section);
    }

    // The first piece of every row of a TAB loop is a candidate. Where more than one piece makes the
    // same tile, the smallest is the starter, the others are transitions and the like.
    struct Found {
        const RUL0::PuzzlePiece* piece;
        RUL0::NetworkType base;
    };
    std::map<uint32_t, Found> found;  // by tile, so the output is in a stable order
    const auto consider = [&](const std::vector<uint32_t>& row) {
        const auto* piece = row.empty() ? nullptr : ResolvePiece(*rul0, row.front());
        if (!piece) {
            return;
        }
        for (const auto& check : piece->checkTypes) {
            if (check.networks.size() != 2) {
                continue;
            }
            const uint32_t tile = SolveCrossing(sections, check.networks[0], check.networks[1]);
            const auto spread = spreading.find(tile);
            if (spread == spreading.end()) {
                continue;
            }
            const auto [entry, isNew] = found.try_emplace(tile, Found{piece, spread->second});
            if (!isNew && piece->checkTypes.size() < entry->second.piece->checkTypes.size()) {
                entry->second.piece = piece;
            }
        }
    };
    for (const auto& ordering : rul0->orderings) {
        consider(ordering.rotationRing);
        for (const auto& row : ordering.addTypes) {
            consider(row);
        }
    }

    std::vector<OverrideNetwork> networks;
    networks.reserve(found.size());
    for (const auto& [tile, entry] : found) {
        networks.push_back({PieceName(index, *entry.piece), tile, static_cast<uint32_t>(entry.base), entry.piece->id});
    }
    spdlog::info("Found {} override networks", networks.size());
    return networks;
}
