#include "ThumbnailStore.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

#include <webp/decode.h>

#include "../utils/Logger.h"

namespace {
    constexpr std::array<char, 4> kMagic = {'S', 'P', 'T', 'H'};
    // Version 1 files were written in BGRA for the old DirectX 7 ImGui backend.
    constexpr uint16_t kVersionBgra = 1;
    constexpr uint16_t kVersionRgba = 2;
    constexpr uint16_t kVersionWebp = 3;
    constexpr uint32_t kHeaderSize = 16;
    constexpr uint32_t kKeySize    = 8;
    constexpr uint32_t kBlobEntrySize = 8;

    // Area-averages the pixels down to fit maxSize. Colour is weighted by alpha so the transparent
    // background does not darken the edges.
    ThumbnailStore::ThumbnailData FitWithin(ThumbnailStore::ThumbnailData src, const uint32_t maxSize) {
        if (maxSize == 0 || (src.width <= maxSize && src.height <= maxSize)) {
            return src;
        }
        const uint32_t larger = std::max(src.width, src.height);
        const uint32_t dw = std::max(1u, src.width * maxSize / larger);
        const uint32_t dh = std::max(1u, src.height * maxSize / larger);
        ThumbnailStore::ThumbnailData dst{std::vector<uint8_t>(size_t{dw} * dh * 4), dw, dh};
        for (uint32_t y = 0; y < dh; ++y) {
            const uint32_t sy0 = y * src.height / dh;
            const uint32_t sy1 = std::max(sy0 + 1, (y + 1) * src.height / dh);
            for (uint32_t x = 0; x < dw; ++x) {
                const uint32_t sx0 = x * src.width / dw;
                const uint32_t sx1 = std::max(sx0 + 1, (x + 1) * src.width / dw);
                uint32_t r = 0, g = 0, b = 0, a = 0, n = 0;
                for (uint32_t sy = sy0; sy < sy1; ++sy) {
                    for (uint32_t sx = sx0; sx < sx1; ++sx) {
                        const uint8_t* p = &src.rgba[(size_t{sy} * src.width + sx) * 4];
                        r += p[0] * p[3];
                        g += p[1] * p[3];
                        b += p[2] * p[3];
                        a += p[3];
                        ++n;
                    }
                }
                uint8_t* q = &dst.rgba[(size_t{y} * dw + x) * 4];
                if (a > 0) {
                    q[0] = static_cast<uint8_t>(r / a);
                    q[1] = static_cast<uint8_t>(g / a);
                    q[2] = static_cast<uint8_t>(b / a);
                }
                q[3] = static_cast<uint8_t>(a / n);
            }
        }
        return dst;
    }
}

void ThumbnailStore::Load(const std::filesystem::path& path) {
    file_.close();
    file_.clear();
    giKeys_.clear();
    blobs_.clear();
    entryCount_ = 0;
    width_ = 0;
    height_ = 0;
    version_ = 0;

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
    if (version != kVersionWebp && version != kVersionRgba && version != kVersionBgra) {
        LOG_ERROR("ThumbnailStore: unsupported version {} in {}", version, path.string());
        file_.close();
        return;
    }

    // Read the sorted gi_key index, and for version 3 the blob table after it.
    giKeys_.resize(count);
    file_.read(reinterpret_cast<char*>(giKeys_.data()), static_cast<std::streamsize>(count) * kKeySize);
    if (version == kVersionWebp) {
        blobs_.resize(count);
        for (auto& [offset, size] : blobs_) {
            file_.read(reinterpret_cast<char*>(&offset), 4);
            file_.read(reinterpret_cast<char*>(&size), 4);
        }
    }

    if (!file_) {
        LOG_ERROR("ThumbnailStore: failed to read index from {}", path.string());
        file_.close();
        giKeys_.clear();
        blobs_.clear();
        return;
    }

    entryCount_ = count;
    width_      = width;
    height_     = height;
    version_    = version;

    LOG_INFO("ThumbnailStore: loaded {} thumbnails ({}x{}, version {}) from {}",
             count, width, height, version, path.string());
}

bool ThumbnailStore::HasThumbnail(const uint64_t giKey) const {
    return std::ranges::binary_search(giKeys_, giKey);
}

std::optional<ThumbnailStore::ThumbnailData> ThumbnailStore::LoadThumbnail(const uint64_t giKey, const uint32_t maxSize) {
    if (!file_.is_open() || giKeys_.empty()) {
        return std::nullopt;
    }

    const auto it = std::ranges::lower_bound(giKeys_, giKey);
    if (it == giKeys_.end() || *it != giKey) {
        return std::nullopt;
    }

    const auto rank = static_cast<size_t>(it - giKeys_.begin());
    const size_t stride = static_cast<size_t>(width_) * height_ * 4;
    const auto indexEnd = static_cast<std::streamoff>(kHeaderSize) +
                          static_cast<std::streamoff>(entryCount_) * kKeySize;

    ThumbnailData result;
    result.width  = width_;
    result.height = height_;
    result.rgba.resize(stride);

    file_.clear();
    if (version_ == kVersionWebp) {
        const auto [blobOffset, blobSize] = blobs_[rank];
        std::vector<uint8_t> blob(blobSize);
        file_.seekg(indexEnd + static_cast<std::streamoff>(entryCount_) * kBlobEntrySize + blobOffset);
        file_.read(reinterpret_cast<char*>(blob.data()), blobSize);
        if (!file_) {
            LOG_ERROR("ThumbnailStore: read failed for rank {}", rank);
            return std::nullopt;
        }

        int blobWidth = 0;
        int blobHeight = 0;
        if (!WebPGetInfo(blob.data(), blob.size(), &blobWidth, &blobHeight) ||
            blobWidth != width_ || blobHeight != height_ ||
            !WebPDecodeRGBAInto(blob.data(), blob.size(), result.rgba.data(), result.rgba.size(),
                                static_cast<int>(width_) * 4)) {
            LOG_ERROR("ThumbnailStore: failed to decode WebP for rank {}", rank);
            return std::nullopt;
        }
        return FitWithin(std::move(result), maxSize);
    }

    file_.seekg(indexEnd + static_cast<std::streamoff>(rank) * static_cast<std::streamoff>(stride));
    file_.read(reinterpret_cast<char*>(result.rgba.data()), static_cast<std::streamsize>(stride));
    if (!file_) {
        LOG_ERROR("ThumbnailStore: read failed for rank {}", rank);
        return std::nullopt;
    }

    if (version_ == kVersionBgra) {
        for (size_t i = 0; i + 3 < result.rgba.size(); i += 4) {
            std::swap(result.rgba[i], result.rgba[i + 2]);
        }
    }

    return FitWithin(std::move(result), maxSize);
}
