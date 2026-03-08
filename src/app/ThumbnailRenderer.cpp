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
        constexpr auto kTypeIdATC = 0x29A5D1ECu;
        constexpr float kHorizontalPadding = 1.12f;
        constexpr float kTopPadding = 1.20f;
        constexpr float kBottomPadding = 1.08f;
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
            if (tgi.type == kTypeIdATC) {
                spdlog::trace("Thumbnail render received an ATC {}, not supported", tgi.ToString());
                return std::nullopt;
            }

            spdlog::warn("Thumbnail renderer received non-S3D unknown {}", tgi.ToString());
            return std::nullopt;
        }

        const auto modelHandle = loadModel_(tgi);
        if (!modelHandle) {
            spdlog::trace("Thumbnail renderer could not build model {}", tgi.ToString());
            return std::nullopt;
        }

        const RenderTexture2D target = LoadRenderTexture(static_cast<int>(size), static_cast<int>(size));
        if (target.id == 0) {
            return std::nullopt;
        }

        const BoundingBox bounds = GetModelBoundingBox(modelHandle->model);
        const Vector3 sizeVec = Vector3Subtract(bounds.max, bounds.min);

        const auto maxDim = std::max(std::max(sizeVec.x, sizeVec.y), sizeVec.z);
        if (maxDim <= 0.001f) {
            spdlog::warn("Thumbnail renderer: degenerate bounds for {} (maxDim={})", tgi.ToString(), maxDim);
            UnloadRenderTexture(target);
            return std::nullopt;
        }
        const Vector3 center = Vector3Scale(Vector3Add(bounds.min, bounds.max), 0.5f);
        const Vector3 framingTarget{
            center.x,
            bounds.min.y,
            center.z
        };

        const float scale = 0.95f * static_cast<float>(size) / (maxDim * 1.414f);

        Camera3D camera{};
        camera.projection = CAMERA_ORTHOGRAPHIC;

        camera.fovy = static_cast<float>(size) / 2.0f;
        camera.up = Vector3{0.0f, 1.0f, 0.0f};
        camera.target = framingTarget;

        constexpr auto kYawRad = 0.785398f; // 45° (π/4) - SW view
        constexpr auto kPitchRadZoom5 = 0.5236f; // ~30° from horizontal
        const Vector3 dir{
            std::cos(kYawRad) * std::cos(kPitchRadZoom5),
            std::sin(kPitchRadZoom5),
            std::sin(kYawRad) * std::cos(kPitchRadZoom5)
        };

        // Keep the model well within raylib's fixed far plane (~1000 units)
        // while staying far enough to avoid near-plane clipping on small props.
        const auto camDistance = std::clamp(maxDim * 12.0f, 80.0f, 1200.0f);
        camera.position = Vector3Add(framingTarget, Vector3Scale(dir, camDistance));

        // Compute ortho size to tightly fit all corners after rotation
        // We need to project all 8 corners of the bounding box onto the camera's view plane
        // and find the maximum extent to ensure the entire model is visible
        Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
        Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
        Vector3 camUp = Vector3Normalize(Vector3CrossProduct(right, forward));

        auto minRight = std::numeric_limits<float>::max();
        auto maxRight = std::numeric_limits<float>::lowest();
        auto minUp = std::numeric_limits<float>::max();
        auto maxUp = std::numeric_limits<float>::lowest();
        for (auto xi = 0; xi < 2; ++xi) {
            for (auto yi = 0; yi < 2; ++yi) {
                for (auto zi = 0; zi < 2; ++zi) {
                    Vector3 corner{
                        xi ? bounds.max.x : bounds.min.x,
                        yi ? bounds.max.y : bounds.min.y,
                        zi ? bounds.max.z : bounds.min.z
                    };
                    // Project the corner onto the camera's right and up vectors
                    // to determine how much screen space it needs
                    Vector3 scaledCorner = Vector3Scale(corner, scale);
                    Vector3 toCorner = Vector3Subtract(scaledCorner, Vector3Scale(framingTarget, scale));
                    const float projRight = Vector3DotProduct(toCorner, right);
                    const float projUp = Vector3DotProduct(toCorner, camUp);
                    minRight = std::min(minRight, projRight);
                    maxRight = std::max(maxRight, projRight);
                    minUp = std::min(minUp, projUp);
                    maxUp = std::max(maxUp, projUp);
                }
            }
        }

        const float horizontalHalfSize = std::max(std::abs(minRight), std::abs(maxRight)) * kHorizontalPadding;
        const float verticalHalfSize = std::max(maxUp * kTopPadding, std::abs(minUp) * kBottomPadding);
        float orthoHalfSize = std::max(horizontalHalfSize, verticalHalfSize);
        if (orthoHalfSize <= 0.0f) {
            orthoHalfSize = static_cast<float>(size) / 2.0f;
        }
        camera.fovy = orthoHalfSize * 2.0f;

        spdlog::trace(
            "Thumbnail renderer camera for {}: maxDim={}, camDistance={}, orthoHalfSize={}, focusY={}",
            tgi.ToString(), maxDim, camDistance, orthoHalfSize, framingTarget.y);

        BeginTextureMode(target);
        ClearBackground(BLANK);
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

        // Evict the model from cache to free GPU resources (textures, meshes).
        // In the cache builder each model is only rendered once, so keeping them
        // around just accumulates VRAM until the process runs out of memory.
        modelCache_.erase(tgi);

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

        auto filePaths = indexService_.lookupFiles(tgi);
        if (filePaths.empty()) {
            failedModels_.insert(tgi);
            return nullptr;
        }

        for (const auto& path : filePaths) {
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
        auto filePaths = indexService_.lookupFiles(tgi);
        if (filePaths.empty()) {
            return std::nullopt;
        }

        for (const auto& path : filePaths) {
            const DBPF::Reader* reader = indexService_.getReader(path);
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
