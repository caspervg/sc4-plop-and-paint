#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <ranges>
#include <utility>
#include <vector>

#include "../shared/entities.hpp"
#include "rfl/visit.hpp"

// Writes a compact indexed binary file of RGBA thumbnails.
//
// Format:
//   Header (16 bytes):
//     [0-3]   char[4]  magic = "SPTH"
//     [4-5]   uint16_t version = 1
//     [6-7]   uint16_t reserved = 0
//     [8-11]  uint32_t entry_count N
//     [12-13] uint16_t width   (all thumbnails share the same dimensions)
//     [14-15] uint16_t height
//
//   Index (N x 8 bytes, sorted ascending by gi_key):
//     [0-7]   uint64_t gi_key   (groupId << 32 | instanceId)
//
//   Data (N blobs in the same order as the index):
//     blob[rank] = width * height * 4 raw RGBA bytes
//     byte_offset(rank) = 16 + N*8 + rank * (width * height * 4)

namespace ThumbnailBin {

    // Writes atomically via a sibling .tmp file. Returns false if the file could not be
    // written or moved into place; throws on malformed thumbnail data.
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

        // Sort by gi_key so the reader can easily binary-search.
        std::ranges::sort(entries, [](const auto& a, const auto& b) { return a.first < b.first; });

        auto tempPath = path;
        tempPath += ".tmp";

        {
            std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
            if (!file) {
                return false;
            }

            // Header
            constexpr char magic[4] = {'S', 'P', 'T', 'H'};
            constexpr uint16_t version = 1;
            constexpr uint16_t reserved = 0;
            const uint32_t count = static_cast<uint32_t>(entries.size());

            file.write(magic, 4);
            file.write(reinterpret_cast<const char*>(&version), 2);
            file.write(reinterpret_cast<const char*>(&reserved), 2);
            file.write(reinterpret_cast<const char*>(&count), 4);
            file.write(reinterpret_cast<const char*>(&commonWidth), 2);
            file.write(reinterpret_cast<const char*>(&commonHeight), 2);

            // Index
            for (const auto& key : entries | std::views::keys) {
                file.write(reinterpret_cast<const char*>(&key), 8);
            }

            // Data
            for (const auto& thumbnail : entries | std::views::values) {
                rfl::visit(
                    [&](const auto& variant) {
                        file.write(reinterpret_cast<const char*>(variant.data.data()),
                                   static_cast<std::streamsize>(variant.data.size()));
                    },
                    thumbnail);
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

} // namespace ThumbnailBin
