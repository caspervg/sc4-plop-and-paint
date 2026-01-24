#include "ModelFactory.hpp"

#include "MeshBuilder.hpp"
#include "raymath.h"
#include "TextureLoader.hpp"
#include "spdlog/spdlog.h"

namespace thumb {
    LoadedModelHandle::~LoadedModelHandle() {
        for (const auto& texture : textures) {
            if (texture.id != 0) {
                UnloadTexture(texture);
            }
        }
        for (const auto& shader : shaders) {
            if (shader.id != 0) {
                UnloadShader(shader);
            }
        }
        if (model.meshCount > 0) {
            UnloadModel(model);
        }
    }

    std::shared_ptr<LoadedModelHandle> ModelFactory::build(
        const S3D::Record& record,
        const DBPF::Tgi tgi,
        const DBPF::Reader& reader,
        const bool previewMode,
        const bool nightMode,
        const bool nightOverlay,
        const float rotationDegrees,
        const std::function<std::optional<FSH::Record>(uint32_t inst, uint32_t group)>& extraTextureLookup) const {
        if (record.animation.animatedMeshes.empty() && record.vertexBuffers.empty()) {
            return nullptr;
        }

        const auto meshSources = MeshBuilder::collectMeshSources(record);
        if (meshSources.empty()) {
            return nullptr;
        }

        const Vector3 center = MeshBuilder::calculateModelCenter(record);
        const auto meshCount = static_cast<int>(meshSources.size());

        Model model{};
        model.transform = MatrixRotateY(DEG2RAD * rotationDegrees);
        model.meshes = static_cast<Mesh*>(MemAlloc(sizeof(Mesh) * meshCount));
        model.materials = static_cast<Material*>(MemAlloc(sizeof(Material) * meshCount));
        model.meshMaterial = static_cast<int*>(MemAlloc(sizeof(int) * meshCount));
        if (!model.meshes || !model.materials || !model.meshMaterial) {
            if (model.meshes) MemFree(model.meshes);
            if (model.materials) MemFree(model.materials);
            if (model.meshMaterial) MemFree(model.meshMaterial);
            return nullptr;
        }

        std::memset(model.meshes, 0, sizeof(Mesh) * meshCount);
        std::memset(model.materials, 0, sizeof(Material) * meshCount);
        std::memset(model.meshMaterial, 0, sizeof(int) * meshCount);

        std::vector<Texture2D> loadedTextures;
        loadedTextures.reserve(meshSources.size());

        auto builtCount = 0;
        const auto cleanup = [&] {
            model.meshCount = builtCount;
            model.materialCount = builtCount;
            if (builtCount > 0) {
                UnloadModel(model);
            }
            else {
                MemFree(model.meshes);
                MemFree(model.materials);
                MemFree(model.meshMaterial);
            }
            for (const auto& texture : loadedTextures) {
                if (texture.id != 0) {
                    UnloadTexture(texture);
                }
            }
        };

        for (const auto& meshSource : meshSources) {
            Mesh mesh{};
            const float yLift = center.y - record.bbMin.y;
            const auto preserveSpace = previewMode;
            if (!MeshBuilder::buildMeshFromSource(meshSource, center, yLift, mesh, preserveSpace)) {
                cleanup();
                return nullptr;
            }

            model.meshes[builtCount] = mesh;
            model.meshMaterial[builtCount] = builtCount;

            Material material = LoadMaterialDefault();
            const auto* matInfo = meshSource.material;

            if (matInfo) {
                for (const auto& texInfo : matInfo->textures) {
                    if (auto texture = TextureLoader::loadTextureForMaterial(reader,
                                                                             tgi,
                                                                             texInfo.textureID,
                                                                             nightMode,
                                                                             nightOverlay,
                                                                             extraTextureLookup)) {
                        if (previewMode) {
                            SetTextureWrap(*texture, TEXTURE_WRAP_CLAMP);
                            SetTextureFilter(*texture, TEXTURE_FILTER_BILINEAR);
                        }
                        else {
                            const int wrapMode = (texInfo.wrapS == 1 || texInfo.wrapT == 1)
                                                     ? TEXTURE_WRAP_CLAMP
                                                     : TEXTURE_WRAP_REPEAT;
                            SetTextureWrap(*texture, wrapMode);
                            const int filter = texInfo.minFilter > 0
                                                   ? TEXTURE_FILTER_BILINEAR
                                                   : TEXTURE_FILTER_POINT;
                            SetTextureFilter(*texture, filter);
                        }

                        loadedTextures.push_back(*texture);
                        Texture2D& storedTexture = loadedTextures.back();
                        SetMaterialTexture(&material, MATERIAL_MAP_DIFFUSE, storedTexture);
                        break;
                    }
                    else {
                        spdlog::debug("Could not load texture for material {}", texInfo.textureID);
                    }
                }
            }

            model.materials[builtCount] = material;
            ++builtCount;
        }

        model.meshCount = builtCount;
        model.materialCount = builtCount;

        auto handle = std::make_shared<LoadedModelHandle>();
        handle->model = model;
        handle->textures = std::move(loadedTextures);

        for (int mi = 0; mi < model.materialCount; ++mi) {
            Shader sh = model.materials[mi].shader;
            if (sh.id != 0) {
                bool seen = false;
                for (const auto& s : handle->shaders) {
                    if (s.id == sh.id) {
                        seen = true;
                        break;
                    }
                }
                if (!seen) {
                    handle->shaders.push_back(sh);
                }
            }
        }

        return handle;
    }
} // namespace thumb
