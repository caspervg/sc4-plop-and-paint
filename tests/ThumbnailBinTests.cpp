// Round-trip checks for the thumbnail cache: run ThumbnailBinTests.exe, it prints "ok" or aborts.
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "app/ThumbnailBinWriter.hpp"
#include "thumbnail/ThumbnailStore.hpp"
#include "utils/Logger.h"

namespace {
    void Check(const bool condition, const char* what, const int line) {
        if (!condition) {
            std::fprintf(stderr, "FAILED line %d: %s\n", line, what);
            std::abort();
        }
    }
#define CHECK(x) Check((x), #x, __LINE__)

    constexpr uint32_t kSize = 16;

    // Opaque square of one colour on a transparent background.
    Thumbnail MakeThumbnail(const std::byte r, const std::byte g, const std::byte b) {
        PreRendered image{rfl::Bytestring(kSize * kSize * 4), kSize, kSize};
        for (uint32_t y = 4; y < 12; ++y) {
            for (uint32_t x = 4; x < 12; ++x) {
                const size_t i = (y * kSize + x) * 4;
                image.data[i] = r;
                image.data[i + 1] = g;
                image.data[i + 2] = b;
                image.data[i + 3] = std::byte{255};
            }
        }
        return image;
    }

    uint32_t ReadU32(const std::filesystem::path& path, const std::streamoff offset) {
        std::ifstream file(path, std::ios::binary);
        file.seekg(offset);
        uint32_t value = 0;
        file.read(reinterpret_cast<char*>(&value), 4);
        return value;
    }
}

int main() try {
    Logger::Initialize("ThumbnailBinTests", "", false);  // debug output only, no log file
    const auto path = std::filesystem::temp_directory_path() / "sc4pnp_thumbnail_test.bin";

    // Keys 1 and 3 share pixels, key 2 differs. Written out of order: the writer sorts.
    std::vector<std::pair<uint64_t, Thumbnail>> entries;
    entries.emplace_back(3, MakeThumbnail(std::byte{200}, std::byte{40}, std::byte{40}));
    entries.emplace_back(2, MakeThumbnail(std::byte{40}, std::byte{40}, std::byte{200}));
    entries.emplace_back(1, MakeThumbnail(std::byte{200}, std::byte{40}, std::byte{40}));
    CHECK(ThumbnailBin::Write(path, std::move(entries)));

    // Blob table after header (16) + keys (3 x 8): identical pixels share one blob.
    constexpr std::streamoff table = 16 + 3 * 8;
    CHECK(ReadU32(path, table) == ReadU32(path, table + 16));      // key 1 and key 3 offsets
    CHECK(ReadU32(path, table) != ReadU32(path, table + 8));       // key 2 has its own blob

    {
        ThumbnailStore store;
        store.Load(path);
        CHECK(store.HasThumbnail(1) && store.HasThumbnail(2) && store.HasThumbnail(3));
        CHECK(!store.HasThumbnail(4));
        CHECK(!store.LoadThumbnail(4).has_value());

        for (const uint64_t key : {1ull, 2ull, 3ull}) {
            const auto thumb = store.LoadThumbnail(key);
            CHECK(thumb.has_value());
            CHECK(thumb->width == kSize && thumb->height == kSize);
            CHECK(thumb->rgba.size() == kSize * kSize * 4);

            const uint8_t* center = &thumb->rgba[(8 * kSize + 8) * 4];
            const uint8_t* corner = &thumb->rgba[0];
            CHECK(center[3] == 255 && corner[3] == 0);  // alpha is stored losslessly
            const int expectedRed = key == 2 ? 40 : 200;
            CHECK(std::abs(center[0] - expectedRed) <= 12);  // lossy colour stays close
        }

        // Scaled down to the panel size: half size keeps the opaque square and transparent border.
        const auto small = store.LoadThumbnail(2, kSize / 2);
        CHECK(small.has_value() && small->width == kSize / 2 && small->height == kSize / 2);
        CHECK(small->rgba.size() == kSize / 2 * kSize / 2 * 4);
        const uint8_t* smallCenter = &small->rgba[(4 * (kSize / 2) + 4) * 4];
        CHECK(smallCenter[3] == 255 && small->rgba[3] == 0);
        CHECK(std::abs(smallCenter[2] - 200) <= 12);  // edge pixels did not darken the colour
        CHECK(store.LoadThumbnail(2, kSize * 2)->width == kSize);  // never scaled up
    }  // closes the file before removing it

    std::filesystem::remove(path);
    std::puts("ok");
    return 0;
}
catch (const std::exception& error) {
    std::fprintf(stderr, "FAILED: %s\n", error.what());
    return 1;
}
