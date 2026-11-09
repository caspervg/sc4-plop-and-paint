#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <webp/encode.h>

#include "../shared/entities.hpp"
#include "rfl/visit.hpp"

// Writes a compact indexed binary file of WebP-compressed thumbnails.
//
// Format:
//   Header (16 bytes):
//     [0-3]   char[4]  magic = "SPTH"
//     [4-5]   uint16_t version = 3 (versions 1 and 2 stored raw BGRA / RGBA pixels)
//     [6-7]   uint16_t reserved = 0
//     [8-11]  uint32_t entry_count N
//     [12-13] uint16_t width   (all thumbnails share the same dimensions)
//     [14-15] uint16_t height
//
//   Index (N x 8 bytes, sorted ascending by gi_key):
//     [0-7]   uint64_t gi_key   (groupId << 32 | instanceId)
//
//   Blob table (N x 8 bytes, in the same order as the index):
//     [0-3]   uint32_t offset   (from the start of the data section)
//     [4-7]   uint32_t size
//
//   Data: WebP blobs (lossy, RGBA). Entries with identical pixels share one blob.

namespace ThumbnailBin {
    constexpr float kWebpQuality = 80.0f;

    inline std::string EncodeWebp(const Thumbnail& thumbnail) {
        std::string blob;
        rfl::visit(
            [&](const auto& variant) {
                uint8_t* out = nullptr;
                const size_t size = WebPEncodeRGBA(reinterpret_cast<const uint8_t*>(variant.data.data()),
                                                   static_cast<int>(variant.width),
                                                   static_cast<int>(variant.height),
                                                   static_cast<int>(variant.width * 4),
                                                   kWebpQuality,
                                                   &out);
                if (size != 0) {
                    blob.assign(reinterpret_cast<const char*>(out), size);
                }
                WebPFree(out);
            },
            thumbnail);
        if (blob.empty()) {
            throw std::runtime_error("ThumbnailBin::Write failed to WebP-encode a thumbnail");
        }
        return blob;
    }

    // Writes WebP blobs that all decode to width x height pixels, keyed by gi_key. Writes atomically via
    // a sibling .tmp file. Returns false if the file could not be written or moved into place.
    inline bool WriteEncoded(const std::filesystem::path& path,
                             std::vector<std::pair<uint64_t, std::string>> entries,
                             const uint16_t width,
                             const uint16_t height) {
        if (entries.empty()) {
            return false;
        }

        // Sort by gi_key so the reader can easily binary-search.
        std::ranges::sort(entries, [](const auto& a, const auto& b) { return a.first < b.first; });

        // Blob table, storing each distinct blob once. The encoder is deterministic, so identical
        // pixels give identical blobs.
        std::vector<std::pair<uint32_t, uint32_t>> table(entries.size());
        std::vector<size_t> uniqueBlobs;
        {
            std::unordered_map<std::string_view, uint32_t> offsetByBlob;
            uint64_t dataSize = 0;
            for (size_t i = 0; i < entries.size(); ++i) {
                const std::string& blob = entries[i].second;
                auto [it, inserted] = offsetByBlob.try_emplace(blob, static_cast<uint32_t>(dataSize));
                if (inserted) {
                    uniqueBlobs.push_back(i);
                    dataSize += blob.size();
                    if (dataSize > UINT32_MAX) {
                        throw std::runtime_error("ThumbnailBin::Write data section exceeds 4 GB");
                    }
                }
                table[i] = {it->second, static_cast<uint32_t>(blob.size())};
            }
        }

        auto tempPath = path;
        tempPath += ".tmp";

        {
            std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
            if (!file) {
                return false;
            }

            // Header
            constexpr char magic[4] = {'S', 'P', 'T', 'H'};
            constexpr uint16_t version = 3;
            constexpr uint16_t reserved = 0;
            const uint32_t count = static_cast<uint32_t>(entries.size());

            file.write(magic, 4);
            file.write(reinterpret_cast<const char*>(&version), 2);
            file.write(reinterpret_cast<const char*>(&reserved), 2);
            file.write(reinterpret_cast<const char*>(&count), 4);
            file.write(reinterpret_cast<const char*>(&width), 2);
            file.write(reinterpret_cast<const char*>(&height), 2);

            // Index
            for (const auto& key : entries | std::views::keys) {
                file.write(reinterpret_cast<const char*>(&key), 8);
            }

            // Blob table
            for (const auto& [offset, size] : table) {
                file.write(reinterpret_cast<const char*>(&offset), 4);
                file.write(reinterpret_cast<const char*>(&size), 4);
            }

            // Data
            for (const size_t i : uniqueBlobs) {
                file.write(entries[i].second.data(), static_cast<std::streamsize>(entries[i].second.size()));
            }

            file.flush();
            if (!file.good()) {
                file.close();
                std::error_code removeEc;
                std::filesystem::remove(tempPath, removeEc);
                return false;
            }
        }

        std::error_code renameEc;
        std::filesystem::rename(tempPath, path, renameEc);
        if (renameEc) {
            std::error_code removeEc;
            std::filesystem::remove(tempPath, removeEc);
            return false;
        }
        return true;
    }

    // Encodes raw thumbnails on all cores and writes them with WriteEncoded. Throws on malformed
    // thumbnail data.
    inline bool Write(const std::filesystem::path& path, std::vector<std::pair<uint64_t, Thumbnail>> entries) {
        if (entries.empty()) {
            return false;
        }

        // Determine common dimensions from first entry (all thumbnails must match).
        uint16_t commonWidth = 0;
        uint16_t commonHeight = 0;
        rfl::visit(
            [&](const auto& variant) {
                commonWidth = static_cast<uint16_t>(variant.width);
                commonHeight = static_cast<uint16_t>(variant.height);
            },
            entries[0].second);

        for (const auto& thumbnail : entries | std::views::values) {
            rfl::visit(
                [&](const auto& variant) {
                    if (variant.width != commonWidth || variant.height != commonHeight) {
                        throw std::runtime_error("ThumbnailBin::Write requires uniform thumbnail dimensions");
                    }
                    const size_t expectedSize = static_cast<size_t>(commonWidth) * commonHeight * 4;
                    if (variant.data.size() != expectedSize) {
                        throw std::runtime_error("ThumbnailBin::Write received malformed RGBA thumbnail data");
                    }
                },
                thumbnail);
        }

        std::vector<std::pair<uint64_t, std::string>> encoded(entries.size());
        {
            std::atomic<size_t> next{0};
            std::atomic<bool> failed{false};
            const auto worker = [&] {
                try {
                    for (size_t i = next++; i < entries.size() && !failed; i = next++) {
                        encoded[i] = {entries[i].first, EncodeWebp(entries[i].second)};
                    }
                }
                catch (...) {
                    failed = true;
                }
            };
            std::vector<std::thread> threads(std::max(1u, std::thread::hardware_concurrency()));
            for (auto& thread : threads) {
                thread = std::thread(worker);
            }
            for (auto& thread : threads) {
                thread.join();
            }
            if (failed) {
                throw std::runtime_error("ThumbnailBin::Write failed to WebP-encode a thumbnail");
            }
        }

        return WriteEncoded(path, std::move(encoded), commonWidth, commonHeight);
    }

} // namespace ThumbnailBin
