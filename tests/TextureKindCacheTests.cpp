// Checks for textures.cbor: run TextureKindCacheTests.exe, it prints "ok" or aborts.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "decals/TextureKindCache.hpp"

namespace {
    void Check(const bool condition, const char* what, const int line) {
        if (!condition) {
            std::fprintf(stderr, "FAILED line %d: %s\n", line, what);
            std::abort();
        }
    }
#define CHECK(x) Check((x), #x, __LINE__)
}

int main() {
    using namespace decals;

    const auto path = std::filesystem::temp_directory_path() / "pnp_texture_kind_cache_test.cbor";
    std::filesystem::remove(path);

    // No file yet.
    CHECK(LoadTextureKindCache(path, 7).base.empty());

    TextureKindCache saved;
    saved.outsideCacheStamp = 133000000000000123;
    saved.base = {4, 0xFFFFFFF4u};
    saved.overlay = {9};
    SaveTextureKindCache(path, saved);

    const TextureKindCache loaded = LoadTextureKindCache(path, saved.outsideCacheStamp);
    CHECK(loaded.outsideCacheStamp == saved.outsideCacheStamp);
    CHECK(loaded.base == saved.base);
    CHECK(loaded.overlay == saved.overlay);

    // The outside cache was rebuilt.
    const TextureKindCache stale = LoadTextureKindCache(path, saved.outsideCacheStamp + 1);
    CHECK(stale.base.empty() && stale.overlay.empty());

    // A damaged file counts as no file.
    std::ofstream(path, std::ios::binary | std::ios::trunc) << "not cbor";
    CHECK(LoadTextureKindCache(path, saved.outsideCacheStamp).base.empty());

    std::filesystem::remove(path);
    std::puts("ok");
    return 0;
}
