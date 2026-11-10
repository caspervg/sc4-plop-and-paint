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
    uint32_t previewModel = 0;  // instance of the model for the starter piece's picture
    uint32_t fallbackModel = 0; // another model to try when that one does not render, 0 for none
    std::vector<OverrideNetworkRow> rows;
    bool diagonal = false;      // a drag can start or go on a diagonal
};

// A puzzle piece: one row of a TAB loop in RUL0. Its picture is in network_thumbnails.bin under
// MakeGIKey(1, id).
struct NetworkPiece {
    std::string name;
    uint32_t id = 0;            // RUL0 ID of the piece, the first of its rotations
    uint32_t previewModel = 0;  // instance of the model for its picture
    uint32_t fallbackModel = 0; // another model to try when that one does not render, 0 for none
    uint32_t rotations = 0;     // how many rotations Home and End go through
    // The piece's cells, row by row from the north: a bit for each network in the cell (bit n is
    // cISC4NetworkOccupant::eNetworkType n), kOtherCell for a cell without a network of its own, 0 for
    // no cell. The cache builder draws this when no model renders.
    std::vector<std::vector<uint32_t>> layout;
    static constexpr uint32_t kOtherCell = 0x8000;
    uint32_t cost = 0;
};

// A button of the game's network menus, with the pieces TAB goes through. The buttons are in the order
// of the game's menus.
struct NetworkPieceButton {
    std::string name;
    std::vector<std::string> path;  // the menu the button is in, then the submenus down to it
    uint32_t id = 0;                // the first piece. The menu button's ID is 0x6A470000 plus this.
    std::vector<NetworkPiece> pieces;
};

struct NetworksCache {
    std::vector<OverrideNetwork> networks;
    std::vector<NetworkPieceButton> buttons;
};
