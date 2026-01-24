#include "TextureLoader.hpp"

#include <optional>
#include <utility>
#include <vector>

#include "spdlog/spdlog.h"

namespace thumb {
    std::optional<Texture2D> TextureLoader::loadTextureForMaterial(
        const DBPF::Reader& reader,
        DBPF::Tgi tgi,
        const uint32_t textureId,
        const bool nightMode,
        const bool nightOverlay,
        std::function<std::optional<FSH::Record>(uint32_t inst, uint32_t group)> extraLookup) {
        const auto tryLoad = [&](uint32_t inst, uint32_t group) -> std::optional<FSH::Record> {
            DBPF::TgiMask mask;
            mask.type = 0x7AB50E44; // FSH
            mask.group = group;
            mask.instance = inst;
            auto fsh = reader.LoadFSH(mask);
            if (!fsh.has_value() || fsh->entries.empty() || fsh->entries[0].bitmaps.empty()) {
                if (extraLookup) {
                    return extraLookup(inst, group);
                }
                return std::nullopt;
            }
            return std::make_optional(std::move(*fsh));
        };

        std::optional<FSH::Record> day;
        std::optional<FSH::Record> night;
        const auto loadDay = [&]() -> std::optional<FSH::Record> {
            if (day) return day;
            day = tryLoad(textureId, tgi.group);
            if (!day) day = tryLoad(textureId, 0x1ABE787D);
            return day;
        };

        if (nightMode) {
            const uint32_t nightInst = textureId + 0x8000u;
            night = tryLoad(nightInst, tgi.group);
            if (!night) {
                night = tryLoad(nightInst, 0x1ABE787D);
            }
        }
        if (!night) {
            night = loadDay();
        }
        if (!night) {
            spdlog::debug("Could not load FSH for texture ID {}", textureId);
            return std::nullopt;
        }

        std::vector<uint8_t> rgba;
        if (nightOverlay && nightMode) {
            auto base = loadDay();
            if (base && !base->entries.empty() && !base->entries[0].bitmaps.empty()) {
                const auto& nightBmp = night->entries[0].bitmaps[0];
                const auto& dayBmp = base->entries[0].bitmaps[0];
                if (nightBmp.width == dayBmp.width && nightBmp.height == dayBmp.height) {
                    std::vector<uint8_t> dayRgba;
                    if (FSH::Reader::ConvertToRGBA8(dayBmp, dayRgba) &&
                        FSH::Reader::ConvertToRGBA8(nightBmp, rgba)) {
                        const size_t pxCount = dayBmp.width * dayBmp.height;
                        for (size_t i = 0; i < pxCount; ++i) {
                            const size_t idx = i * 4;
                            const float a = static_cast<float>(rgba[idx + 3]) / 255.0f;
                            dayRgba[idx + 0] = static_cast<uint8_t>(dayRgba[idx + 0] * (1.0f - a) + rgba[idx + 0] * a);
                            dayRgba[idx + 1] = static_cast<uint8_t>(dayRgba[idx + 1] * (1.0f - a) + rgba[idx + 1] * a);
                            dayRgba[idx + 2] = static_cast<uint8_t>(dayRgba[idx + 2] * (1.0f - a) + rgba[idx + 2] * a);
                            dayRgba[idx + 3] = dayRgba[idx + 3] > rgba[idx + 3] ? dayRgba[idx + 3] : rgba[idx + 3];
                        }
                        rgba.swap(dayRgba);
                    }
                }
            }
        }

        if (rgba.empty()) {
            if (!FSH::Reader::ConvertToRGBA8(night->entries[0].bitmaps[0], rgba)) {
                return std::nullopt;
            }
        }

        Image img{
            .data = rgba.data(),
            .width = night->entries[0].bitmaps[0].width,
            .height = night->entries[0].bitmaps[0].height,
            .mipmaps = 1,
            .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
        };

        Texture2D texture = LoadTextureFromImage(img);
        if (texture.id == 0) {
            return std::nullopt;
        }
        return texture;
    }
} // namespace thumb
