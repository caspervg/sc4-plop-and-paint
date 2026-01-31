#include "ThumbnailRenderer.hpp"

#include <algorithm>
#include <cmath>

#include "ModelFactory.hpp"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "S3DStructures.h"
#include "spdlog/spdlog.h"

namespace thumb {
    namespace {
        constexpr auto kTypeIdS3D = 0x5AD0E817u;
        constexpr auto kTypeIdFSH = 0x7AB50E44u;
    }

    ThumbnailRenderer::ThumbnailRenderer(const DbpfIndexService& indexService)
        : indexService_(indexService),
          modelFactory_(std::make_shared<ModelFactory>()) {}

    ThumbnailRenderer::~ThumbnailRenderer() {
        if (initialized_) {
            CloseWindow();
        }
    }

    std::optional<RenderedImage> ThumbnailRenderer::renderModel(const DBPF::Tgi& tgi, const uint32_t size) {
        if (size == 0) {
            return std::nullopt;
        }
        if (!ensureInitialized_()) {
            spdlog::warn("Thumbnail renderer failed to initialize raylib");
            return std::nullopt;
        }

        if (tgi.type != kTypeIdS3D) {
            spdlog::warn("Thumbnail renderer received non-S3D TGI {}", tgi.ToString());
            return std::nullopt;
        }

        const auto modelHandle = loadModel_(tgi);
        if (!modelHandle) {
            return std::nullopt;
        }

        const RenderTexture2D target = LoadRenderTexture(static_cast<int>(size), static_cast<int>(size));
        if (target.id == 0) {
            return std::nullopt;
        }

        const BoundingBox bounds = GetModelBoundingBox(modelHandle->model);
        const Vector3 sizeVec = Vector3Subtract(bounds.max, bounds.min);

        const auto maxDim = std::max(std::max(sizeVec.x, sizeVec.y), sizeVec.z);
        const Vector3 center = Vector3Scale(Vector3Add(bounds.min, bounds.max), 0.5f);

        const float scale = 0.95f * static_cast<float>(size) / (maxDim * 1.414f);

        Camera3D camera{};
        camera.projection = CAMERA_ORTHOGRAPHIC;

        camera.fovy = static_cast<float>(size) / 2.0f;
        camera.up = Vector3{0.0f, 1.0f, 0.0f};
        const Vector3 adjustedTarget = {center.x, 2 * center.y, center.z};
        camera.target = adjustedTarget;

        constexpr auto kYawRad = 0.785398f; // 45° (π/4) - SW view
        constexpr auto kPitchRadZoom5 = 0.5236f; // ~30° from horizontal
        const Vector3 dir{
            std::cos(kYawRad) * std::cos(kPitchRadZoom5),
            std::sin(kPitchRadZoom5),
            std::sin(kYawRad) * std::cos(kPitchRadZoom5)
        };

        const auto camDistance = maxDim * 10.0f;
        camera.position = Vector3Add(adjustedTarget, Vector3Scale(dir, camDistance));

        // Compute ortho size to tightly fit all corners after rotation
        Vector3 right = Vector3CrossProduct(dir, camera.up);
        if (Vector3Length(right) == 0.0f) {
            right = Vector3{1.0f, 0.0f, 0.0f};
        }
        right = Vector3Normalize(right);
        Vector3 camUp = Vector3Normalize(Vector3CrossProduct(right, dir));

        auto halfWidth = 0.0f;
        auto halfHeight = 0.0f;
        for (auto xi = 0; xi < 2; ++xi) {
            for (auto yi = 0; yi < 2; ++yi) {
                for (auto zi = 0; zi < 2; ++zi) {
                    Vector3 corner{
                        xi ? bounds.max.x : bounds.min.x,
                        yi ? bounds.max.y : bounds.min.y,
                        zi ? bounds.max.z : bounds.min.z
                    };
                    Vector3 offset = Vector3Scale(Vector3Subtract(corner, adjustedTarget), scale);
                    const float projRight = std::abs(Vector3DotProduct(offset, right));
                    const float projUp = std::abs(Vector3DotProduct(offset, camUp));
                    halfWidth = std::max(halfWidth, projRight);
                    halfHeight = std::max(halfHeight, projUp);
                }
            }
        }

        float orthoHalfSize = std::max(halfWidth, halfHeight) * 1.05f;
        if (orthoHalfSize <= 0.0f) {
            orthoHalfSize = scale;
        }
        camera.fovy = orthoHalfSize * 2.0f;

        BeginTextureMode(target);
        ClearBackground(Color{0, 0, 0, 0});
        BeginMode3D(camera);
        const Vector3 renderScale{scale, scale, scale};
        rlDisableBackfaceCulling();
        DrawModelEx(modelHandle->model, Vector3Zero(), Vector3{0, 1, 0}, 0.0f,
                    renderScale, WHITE);
        rlEnableBackfaceCulling();
        EndMode3D();
        EndTextureMode();

        Image image = LoadImageFromTexture(target.texture);
        ImageFlipVertical(&image);
        ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

        RenderedImage rendered;
        rendered.width = size;
        rendered.height = size;
        rendered.pixels.resize(size * size * 4);
        if (image.data) {
            std::memcpy(rendered.pixels.data(), image.data, rendered.pixels.size());
            for (size_t i = 0; i + 3 < rendered.pixels.size(); i += 4) {
                std::byte tmp = rendered.pixels[i];
                rendered.pixels[i] = rendered.pixels[i + 2];
                rendered.pixels[i + 2] = tmp;
            }
        }

        UnloadImage(image);
        UnloadRenderTexture(target);
        return rendered;
    }

    bool ThumbnailRenderer::ensureInitialized_() {
        if (initialized_) {
            return true;
        }

        SetTraceLogLevel(LOG_WARNING);
        SetConfigFlags(FLAG_WINDOW_HIDDEN);
        InitWindow(1, 1, "SC4ThumbnailRenderer");
        initialized_ = IsWindowReady();
        return initialized_;
    }

    std::shared_ptr<LoadedModelHandle> ThumbnailRenderer::loadModel_(const DBPF::Tgi& tgi) {
        if (auto it = modelCache_.find(tgi); it != modelCache_.end()) {
            return it->second;
        }
        if (failedModels_.contains(tgi)) {
            return nullptr;
        }

        const auto& tgiIndex = indexService_.tgiIndex();
        const auto indexIt = tgiIndex.find(tgi);
        if (indexIt == tgiIndex.end()) {
            failedModels_.insert(tgi);
            return nullptr;
        }

        for (const auto& path : indexIt->second) {
            DBPF::Reader* reader = indexService_.getReader(path);
            if (!reader) {
                continue;
            }

            auto record = reader->LoadS3D(tgi);
            if (!record.has_value()) {
                continue;
            }

            auto model = modelFactory_->build(*record,
                                              tgi,
                                              *reader,
                                              false,
                                              false,
                                              false,
                                              0.0f,
                                              [this](uint32_t inst, uint32_t group) {
                                                  return loadTexture_(inst, group);
                                              });
            if (model) {
                modelCache_[tgi] = model;
                return model;
            }
        }

        failedModels_.insert(tgi);
        return nullptr;
    }

    std::optional<FSH::Record> ThumbnailRenderer::loadTexture_(uint32_t inst, uint32_t group) const {
        DBPF::Tgi tgi{kTypeIdFSH, group, inst};
        const auto& tgiIndex = indexService_.tgiIndex();
        const auto indexIt = tgiIndex.find(tgi);
        if (indexIt == tgiIndex.end()) {
            return std::nullopt;
        }

        for (const auto& path : indexIt->second) {
            DBPF::Reader* reader = indexService_.getReader(path);
            if (!reader) {
                continue;
            }
            auto record = reader->LoadFSH(tgi);
            if (record.has_value()) {
                return *record;
            }
        }

        return std::nullopt;
    }
} // namespace thumb
