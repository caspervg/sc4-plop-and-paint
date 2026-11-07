#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <utility>
#include <vector>

// Reads the compact indexed binary thumbnail file produced by ThumbnailBinWriter.
//
// The file layout is:
//   Header (16 bytes): magic "SPTH", version, entry_count, common width/height.
//   Index  (N x 8 bytes): uint64_t gi_keys, sorted ascending.
//   Version 3: blob table (N x {uint32_t offset, uint32_t size}), then WebP blobs.
//   Versions 1-2: N raw blobs of width * height * 4 bytes (BGRA in version 1, which
//                 LoadThumbnail converts so callers always get RGBA).
//
// The index is binary-searched so HasThumbnail / LoadThumbnail are O(log N).
class ThumbnailStore {
public:
    ThumbnailStore() = default;
    ~ThumbnailStore() = default;

    ThumbnailStore(const ThumbnailStore&) = delete;
    ThumbnailStore& operator=(const ThumbnailStore&) = delete;

    ThumbnailStore(ThumbnailStore&&) noexcept = default;
    ThumbnailStore& operator=(ThumbnailStore&&) noexcept = default;

    void Load(const std::filesystem::path& path);

    [[nodiscard]] bool HasThumbnail(uint64_t giKey) const;

    // Side of the stored (square) thumbnails in pixels, 0 when nothing is loaded.
    [[nodiscard]] uint32_t StoredSize() const { return width_; }

    struct ThumbnailData {
        std::vector<uint8_t> rgba;
        uint32_t width;
        uint32_t height;
    };

    // Seeks to the entry, reads pixel bytes, and returns them, scaled down to fit maxSize
    // (0 keeps the stored size). Never scales up.
    // Returns nullopt if the key is not in the index or the file is not loaded.
    [[nodiscard]] std::optional<ThumbnailData> LoadThumbnail(uint64_t giKey, uint32_t maxSize = 0);

private:
    std::ifstream file_;
    uint32_t entryCount_ = 0;
    uint16_t width_      = 0;
    uint16_t height_     = 0;
    uint16_t version_    = 0;
    std::vector<uint64_t> giKeys_; // sorted, index position == data blob rank
    std::vector<std::pair<uint32_t, uint32_t>> blobs_; // version 3: {offset, size} per rank
};
