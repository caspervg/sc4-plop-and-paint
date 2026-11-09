#pragma once
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "PackCatalogRepository.hpp"

// The "Pack" filter of the Props and Flora tabs: how many items each pack has, a dropdown to pick
// one, and whether an item is in the picked pack. An item shipped in several packs counts, and
// shows, under each of them.
class PackFilter {
public:
    static constexpr int32_t kAllPacks = -2;
    static constexpr int32_t kNoPack = -1;  // items the catalog does not know

    // Counting, once per list of items: Begin, then Count for every item.
    void Begin(const PackCatalogRepository* catalog);
    void Count(std::span<const PackCatalogRepository::ExemplarRow> rows);

    // The dropdown; true when the picked pack changed. Nothing without a catalog.
    bool Render(const char* id, const char* itemsName);

    [[nodiscard]] bool Passes(std::span<const PackCatalogRepository::ExemplarRow> rows) const;
    // Whether the name of a pack the item ships in contains the text, ignoring case.
    [[nodiscard]] bool PackNameContains(std::span<const PackCatalogRepository::ExemplarRow> rows,
                                        std::string_view text) const;

    [[nodiscard]] int32_t Selected() const { return selected_; }
    void Clear() { selected_ = kAllPacks; }
    [[nodiscard]] bool IsActive() const { return selected_ != kAllPacks; }

private:
    const PackCatalogRepository* catalog_ = nullptr;
    int32_t selected_ = kAllPacks;
    // By pack index; the last entry counts the items not in the catalog.
    std::vector<uint32_t> counts_{};
    size_t total_ = 0;
    std::vector<int32_t> byName_{};
    bool sorted_ = false;
};
