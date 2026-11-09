#pragma once
#include <cstdint>
#include <string>
#include <vector>

// networks.cbor: the override networks of the installed Network Addon Mod, such as RHW-4, TLA-3 or the
// SAM sets. The cache builder finds them from the starter pieces in RUL0, so nothing here is a fixed
// list. The DLL shows a button for each one.
struct OverrideNetwork {
    std::string name;           // the starter piece's name, without "Starter Piece"
    uint32_t tile = 0;          // its orthogonal straight tile, which spreads over the base network
    uint32_t baseNetwork = 0;   // cISC4NetworkOccupant::eNetworkType of the network to drag
    uint32_t starterPiece = 0;  // RUL0 ID of the starter piece
};

struct NetworksCache {
    std::vector<OverrideNetwork> networks;
};
