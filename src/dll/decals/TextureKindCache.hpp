#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace decals {

// The contents of textures.cbor: which textures are base and overlay, so each one is read from its
// file only once. outsideCacheStamp ties it to the cache the builder made.
struct TextureKindCache {
    int64_t outsideCacheStamp = 0;
    std::vector<uint32_t> base;
    std::vector<uint32_t> overlay;
};

// Empty when the file is missing or unreadable, or was made for another build of the outside cache.
[[nodiscard]] TextureKindCache LoadTextureKindCache(const std::filesystem::path& path, int64_t outsideCacheStamp);
void SaveTextureKindCache(const std::filesystem::path& path, const TextureKindCache& cache);

}  // namespace decals
