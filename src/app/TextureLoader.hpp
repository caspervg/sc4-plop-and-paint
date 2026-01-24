#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include "DBPFReader.h"
#include "FSHReader.h"
#include "raylib.h"

namespace thumb {
    class TextureLoader {
    public:
        static std::optional<Texture2D> loadTextureForMaterial(
            const DBPF::Reader& reader,
            DBPF::Tgi tgi,
            uint32_t textureId,
            bool nightMode,
            bool nightOverlay,
            std::function<std::optional<FSH::Record>(uint32_t inst, uint32_t group)> extraLookup = {});
    };
} // namespace thumb
