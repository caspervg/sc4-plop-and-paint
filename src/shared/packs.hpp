#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// packs.cbor: where ground textures and props first shipped, from the SC4 Prop Texture Catalog by
// Noah Severyn (https://github.com/noah-severyn/SC4PropTextureCatalog). The cache builder fetches
// the catalog and writes this file; the DLL reads it. Plugins are often datpacked into big files, so the file
// something is in today says little about where it came from.

// Raised when the cache builder starts writing more into packs.cbor, so it fetches the catalog again
// instead of keeping an older file whose download has not changed. 2: props, and every pack an item
// ships in.
constexpr uint32_t kPackCatalogFormat = 2;

struct TexturePack {
    std::string name;     // the download's name
    std::string package;  // its sc4pac package, if any
    std::string author;
    std::string url;
};

struct PackCatalog {
    uint32_t format = 0;  // the cache builder writes kPackCatalogFormat; older files have none
    std::string source;
    // ISO 8601, UTC: the newest download the catalog knows of, and when the cache builder fetched it.
    std::string catalogUpdatedAt;
    std::string fetchedAt;
    // The catalog download's ETag, so the next fetch is skipped while it has not changed.
    std::string etag;
    std::vector<TexturePack> packs;
    // Names of the files things first shipped in, without folder or extension.
    std::vector<std::string> files;
    // Lot and ground textures: zoom 5 instance (type 0x7AB50E44, group 0x0986135E), index into
    // files, index into packs. Sorted; an instance has a row for every pack and file that ships it,
    // since mods often override one another's textures.
    std::vector<std::array<uint32_t, 3>> textures;
    // Props: exemplar group, exemplar instance (as in props.cbor), index into files, index into
    // packs. Sorted, a row per pack and file like textures.
    std::vector<std::array<uint32_t, 4>> props;
};
