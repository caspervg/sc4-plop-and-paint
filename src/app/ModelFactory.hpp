#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "DBPFReader.h"
#include "FSHReader.h"
#include "raylib.h"

namespace thumb {
    struct LoadedModelHandle {
        Model model{};
        std::vector<Texture2D> textures;
        std::vector<Shader> shaders;
        ~LoadedModelHandle();
    };

    class ModelFactory {
    public:
        std::shared_ptr<LoadedModelHandle> build(
            const S3D::Record& record,
            DBPF::Tgi tgi,
            const DBPF::Reader& reader,
            bool previewMode,
            bool nightMode,
            bool nightOverlay,
            float rotationDegrees,
            const std::function<std::optional<FSH::Record>(uint32_t inst, uint32_t group)>& extraTextureLookup = {}) const;
    };
} // namespace thumb
