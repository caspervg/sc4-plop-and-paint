#include "ThumbnailStore.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#include "../utils/Logger.h"

namespace {
    constexpr std::array<char, 4> kMagic = {'S', 'P', 'T', 'H'};
    constexpr uint16_t kVersion = 1;
    constexpr uint32_t kHeaderSize = 16;
    constexpr uint32_t kKeySize    = 8;
}

void ThumbnailStore::Load(const std::filesystem::path& path) {
    file_.close();
    file_.clear();
    giKeys_.clear();
    entryCount_ = 0;
    width_ = 0;
    height_ = 0;

    if (!std::filesystem::exists(path)) {
        LOG_WARN("ThumbnailStore: file not found: {}", path.string());
        return;
    }

    file_.open(path, std::ios::binary);
    if (!file_) {
        LOG_ERROR("ThumbnailStore: failed to open {}", path.string());
        return;
    }

    // Read and validate header.
    std::array<char, 4> magic{};
    uint16_t version  = 0;
    uint16_t reserved = 0;
    uint32_t count    = 0;
    uint16_t width    = 0;
    uint16_t height   = 0;

    file_.read(magic.data(), 4);
    file_.read(reinterpret_cast<char*>(&version),  2);
    file_.read(reinterpret_cast<char*>(&reserved), 2);
    file_.read(reinterpret_cast<char*>(&count),    4);
    file_.read(reinterpret_cast<char*>(&width),    2);
    file_.read(reinterpret_cast<char*>(&height),   2);

    if (!file_) {
        LOG_ERROR("ThumbnailStore: failed to read header from {}", path.string());
        file_.close();
        return;
    }
    if (magic != kMagic) {
        LOG_ERROR("ThumbnailStore: bad magic in {}", path.string());
        file_.close();
        return;
    }
    if (version != kVersion) {
        LOG_ERROR("ThumbnailStore: unsupported version {} in {}", version, path.string());
        file_.close();
        return;
    }

    // Read the sorted gi_key index.
    giKeys_.resize(count);
    file_.read(reinterpret_cast<char*>(giKeys_.data()), count * kKeySize);

    if (!file_) {
        LOG_ERROR("ThumbnailStore: failed to read index from {}", path.string());
        file_.close();
        giKeys_.clear();
        return;
    }

    entryCount_ = count;
    width_      = width;
    height_     = height;

    LOG_INFO("ThumbnailStore: loaded {} thumbnails ({}x{}) from {}",
             count, width, height, path.string());
}

bool ThumbnailStore::HasThumbnail(const uint64_t giKey) const {
    return std::ranges::binary_search(giKeys_, giKey);
}

std::optional<ThumbnailStore::ThumbnailData> ThumbnailStore::LoadThumbnail(const uint64_t giKey) {
    if (!file_.is_open() || giKeys_.empty()) {
        return std::nullopt;
    }

    const auto it = std::ranges::lower_bound(giKeys_, giKey);
    if (it == giKeys_.end() || *it != giKey) {
        return std::nullopt;
    }

    const auto rank   = static_cast<uint32_t>(it - giKeys_.begin());
    const uint32_t stride = static_cast<uint32_t>(width_) * height_ * 4;
    const std::streampos offset =
        static_cast<std::streampos>(kHeaderSize) +
        static_cast<std::streampos>(entryCount_ * kKeySize) +
        static_cast<std::streampos>(rank * stride);

    file_.clear();
    file_.seekg(offset);
    if (!file_) {
        LOG_ERROR("ThumbnailStore: seek failed for rank {}", rank);
        return std::nullopt;
    }

    ThumbnailData result;
    result.width  = width_;
    result.height = height_;
    result.rgba.resize(stride);

    file_.read(reinterpret_cast<char*>(result.rgba.data()), stride);

    if (!file_) {
        LOG_ERROR("ThumbnailStore: read failed for rank {}", rank);
        return std::nullopt;
    }

    return result;
}
