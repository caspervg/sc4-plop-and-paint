#pragma once
#include <vector>

#include "../shared/networks.hpp"

class DbpfIndexService;

namespace NetworkDiscovery {
    // Finds the override networks that the loaded RUL files define. A starter piece puts down one tile
    // (RUL0 says which networks cross there, RUL1 which tile that makes) and RUL2 lets that tile spread
    // over a plain base network. Every piece that does this is an override network.
    [[nodiscard]] std::vector<OverrideNetwork> FindOverrideNetworks(const DbpfIndexService& index);
}
