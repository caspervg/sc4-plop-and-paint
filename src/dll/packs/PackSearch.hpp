#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

// The catalog rows of all members of a family or collection, one per file and pack, so that the
// group shows, and counts once, under each file and pack that one of its members ships in.
[[nodiscard]] inline std::vector<std::array<uint32_t, 4>> MergePackRows(std::vector<std::array<uint32_t, 4>> rows) {
    for (auto& row : rows) {
        row[0] = 0;
        row[1] = 0;
    }
    std::ranges::sort(rows);
    rows.erase(std::ranges::unique(rows).begin(), rows.end());
    return rows;
}
