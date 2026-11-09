#pragma once
#include <cstdint>
#include <string>
#include <vector>

// networks.cbor: the override networks of the installed Network Addon Mod, such as RHW-4, TLA-3 or the
// SAM sets. The cache builder finds them from the starter pieces in RUL0, so nothing here is a fixed
// list. The DLL shows a button for each one. network_thumbnails.bin has a picture of each starter
// piece, keyed by MakeGIKey(0, starterPiece).

// One row of tiles. A wide network such as RHW-6C has several rows side by side.
struct OverrideNetworkRow {
    uint32_t tile = 0;     // orthogonal straight tile of the row, which spreads over the base network
    int32_t offset = 0;    // cells to the east of the middle row, when the network heads south
    uint32_t rotFlip = 0;  // rotation (0-3) of the tile when the network heads south, 0x80 when flipped
};

struct OverrideNetwork {
    std::string name;           // the starter piece's name, without "Starter Piece"
    std::string group;          // name of the menu button the starter piece is under
    uint32_t baseNetwork = 0;   // cISC4NetworkOccupant::eNetworkType of the network to drag
    uint32_t starterPiece = 0;  // RUL0 ID of the starter piece
    uint32_t previewModel = 0;  // instance of the starter piece's preview model
    std::vector<OverrideNetworkRow> rows;
};

struct NetworksCache {
    std::vector<OverrideNetwork> networks;
};
