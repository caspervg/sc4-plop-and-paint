#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

// Reads the compact indexed binary thumbnail file produced by ThumbnailBinWriter.
//
// The file layout is:
//   Header (16 bytes): magic "SPTH", version, entry_count, common width/height.
//   Index  (N x 8 bytes): uint64_t gi_keys, sorted ascending.
//   Data   (N blobs): width * height * 4 RGBA bytes each.
//
// The byte offset of blob[rank] = 16 + N*8 + rank * (width * height * 4).
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

    struct ThumbnailData {
        std::vector<uint8_t> rgba;
        uint32_t width;
        uint32_t height;
    };

    // Seeks to the entry, reads pixel bytes, and returns them.
    // Returns nullopt if the key is not in the index or the file is not loaded.
    [[nodiscard]] std::optional<ThumbnailData> LoadThumbnail(uint64_t giKey);

private:
    std::ifstream file_;
    uint32_t entryCount_ = 0;
    uint16_t width_      = 0;
    uint16_t height_     = 0;
    std::vector<uint64_t> giKeys_; // sorted, index position == data blob rank
};
