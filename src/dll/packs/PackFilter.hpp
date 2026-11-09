#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "PackCatalogRepository.hpp"

// The "Pack" filter of the Props, Flora and Terrain Paint tabs: how many items each pack and file
// has, a dropdown to pick one, and whether an item is in the pick. An item shipped in several
// packs or files counts, and shows, under each of them.
class PackFilter {
public:
    static constexpr int32_t kAllPacks = -2;
    static constexpr int32_t kNoPack = -1;  // items the catalog does not know

    // Counting, once per list of items: Begin, then Count for every item.
    void Begin(const PackCatalogRepository* catalog);
    void Count(std::span<const PackCatalogRepository::ExemplarRow> rows);
    void Count(std::span<const PackCatalogRepository::TextureRow> rows);

    // The dropdown; true when the picked pack changed. Nothing without a catalog.
    bool Render(const char* id, const char* itemsName);

    [[nodiscard]] bool Passes(std::span<const PackCatalogRepository::ExemplarRow> rows) const;
    [[nodiscard]] bool Passes(std::span<const PackCatalogRepository::TextureRow> rows) const;
    // Whether the name of a pack the item ships in contains the text, ignoring case.
    [[nodiscard]] bool PackNameContains(std::span<const PackCatalogRepository::ExemplarRow> rows,
                                        std::string_view text) const;

    [[nodiscard]] int32_t Selected() const { return selected_; }
    void Clear() { selected_ = kAllPacks; }
    [[nodiscard]] bool IsActive() const { return selected_ != kAllPacks; }

private:
    // This and lower pick a file: kFirstFile minus its index.
    static constexpr int32_t kFirstFile = -3;

    template <size_t N>
    void Count_(std::span<const std::array<uint32_t, N>> rows);
    template <size_t N>
    [[nodiscard]] bool Passes_(std::span<const std::array<uint32_t, N>> rows) const;
    [[nodiscard]] std::string Label_(int32_t choice) const;
    void RefreshMatches_();

    const PackCatalogRepository* catalog_ = nullptr;
    int32_t selected_ = kAllPacks;
    char search_[64]{};
    // By pack index; the last entry counts the items not in the catalog.
    std::vector<uint32_t> counts_{};
    size_t total_ = 0;
    std::vector<int32_t> byName_{};
    // By file index: its items, and the first pack that has it.
    std::vector<uint32_t> fileCounts_{};
    std::vector<uint32_t> filePacks_{};
    std::vector<uint32_t> filesByName_{};
    bool sorted_ = false;
    // What search_ finds. Files only once something is typed: there are thousands.
    std::vector<int32_t> packMatches_{};
    std::vector<uint32_t> fileMatches_{};
    std::string appliedSearch_{};
    bool matchesValid_ = false;
};
