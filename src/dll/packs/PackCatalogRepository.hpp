#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "../../shared/packs.hpp"

// packs.cbor from the Plugins folder: which packs and files ground textures and props first shipped
// in. Mods often override one another, so one item can have several: the lookups return every row,
// in the catalog's order. Loaded once; without the file every lookup finds nothing.
class PackCatalogRepository {
public:
    // A texture row: instance, index into files, index into Packs().
    using TextureRow = std::array<uint32_t, 3>;
    // A prop row: exemplar group, exemplar instance, index into files, index into Packs().
    using PropRow = std::array<uint32_t, 4>;

    void Load();

    [[nodiscard]] const std::vector<TexturePack>& Packs() const { return catalog_.packs; }
    [[nodiscard]] std::span<const TextureRow> TextureRows(uint32_t instanceId) const;
    [[nodiscard]] std::span<const PropRow> PropRows(uint32_t groupId, uint32_t instanceId) const;
    // nullptr for an index outside Packs(), such as -1.
    [[nodiscard]] const TexturePack* Pack(int32_t index) const;
    [[nodiscard]] const std::string* File(uint32_t index) const;

private:
    PackCatalog catalog_{};
};
