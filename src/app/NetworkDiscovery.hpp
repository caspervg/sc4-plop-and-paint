#pragma once
#include <vector>

#include "../shared/networks.hpp"

class DbpfIndexService;

namespace NetworkDiscovery {
    // Finds the override networks that the loaded RUL files define. A starter piece puts down one tile
    // (RUL0 says which networks cross there, RUL1 which tile that makes) and RUL2 lets that tile spread
    // over a plain base network. Every piece that does this is an override network.
    //
    // Also lists every menu button with a TAB loop and the pieces in it, under the menus and submenus
    // the button is in.
    [[nodiscard]] NetworksCache Discover(const DbpfIndexService& index);
}
