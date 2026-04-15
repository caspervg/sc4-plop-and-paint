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
        constexpr bool kEnableSilhouettePostFit = true;
        constexpr uint32_t kSupersampleFactor = 2;
        constexpr uint8_t kAlphaFitThreshold = 12;
        constexpr float kPostFitMarginRatio = 0.08f;
        constexpr float kHorizontalPadding = 1.06f;
        constexpr float kTopPadding = 1.06;
        constexpr float kBottomPadding = 1.06f;
        constexpr size_t kSc4ZoomCount = 5;
        constexpr float kSc4DefaultYawRadians = 1.17809725f; // 67.5 degrees
        constexpr float kSc4DefaultPitchRadians[kSc4ZoomCount] = {
            0.52359879f, // 30 degrees
            0.61086524f, // 35 degrees
            0.69813168f, // 40 degrees
            0.78539819f, // 45 degrees
            0.78539819f  // 45 degrees
        };
        constexpr size_t kThumbnailZoomIndex = 4; // SC4 zoom 5 framing

        struct AlphaBounds {
            int minX;
            int minY;
            int maxX;
            int maxY;
        };

        std::optional<AlphaBounds> FindVisibleAlphaBounds(const Image& image, const uint8_t alphaThreshold) {
            if (!image.data || image.width <= 0 || image.height <= 0 || image.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) {
                return std::nullopt;
            }

            const auto* pixels = static_cast<const uint8_t*>(image.data);
            AlphaBounds bounds{
                .minX = image.width,
                .minY = image.height,
                .maxX = -1,
                .maxY = -1
            };

            for (int y = 0; y < image.height; ++y) {
                for (int x = 0; x < image.width; ++x) {
                    const size_t index = (static_cast<size_t>(y) * image.width + x) * 4;
                    if (pixels[index + 3] < alphaThreshold) {
                        continue;
                    }
                    bounds.minX = std::min(bounds.minX, x);
                    bounds.minY = std::min(bounds.minY, y);
                    bounds.maxX = std::max(bounds.maxX, x);
                    bounds.maxY = std::max(bounds.maxY, y);
                }
            }

            if (bounds.maxX < bounds.minX || bounds.maxY < bounds.minY) {
                return std::nullopt;
            }

            return bounds;
        }
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

        const uint32_t renderSize = size * kSupersampleFactor;
        const RenderTexture2D target = LoadRenderTexture(static_cast<int>(renderSize), static_cast<int>(renderSize));
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

        Camera3D camera{};
        camera.projection = CAMERA_ORTHOGRAPHIC;

        camera.fovy = static_cast<float>(size) / 2.0f;
        camera.up = Vector3{0.0f, 1.0f, 0.0f};
        camera.target = framingTarget;

        const Vector3 dir{
            std::cos(kSc4DefaultYawRadians) * std::cos(kSc4DefaultPitchRadians[kThumbnailZoomIndex]),
            std::sin(kSc4DefaultPitchRadians[kThumbnailZoomIndex]),
            std::sin(kSc4DefaultYawRadians) * std::cos(kSc4DefaultPitchRadians[kThumbnailZoomIndex])
        };

        // Compute ortho size to tightly fit all corners after rotation
        // We project all 8 corners of the bounding box onto the camera basis and
        // compute both viewport extents and the depth needed to keep the camera
        // in front of the closest geometry.
        Vector3 forward = Vector3Normalize(Vector3Negate(dir));
        Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
        Vector3 camUp = Vector3Normalize(Vector3CrossProduct(right, forward));

        auto minRight = std::numeric_limits<float>::max();
        auto maxRight = std::numeric_limits<float>::lowest();
        auto minUp = std::numeric_limits<float>::max();
        auto maxUp = std::numeric_limits<float>::lowest();
        auto minForward = std::numeric_limits<float>::max();
        auto maxForward = std::numeric_limits<float>::lowest();
        for (auto xi = 0; xi < 2; ++xi) {
            for (auto yi = 0; yi < 2; ++yi) {
                for (auto zi = 0; zi < 2; ++zi) {
                    Vector3 corner{
                        xi ? bounds.max.x : bounds.min.x,
                        yi ? bounds.max.y : bounds.min.y,
                        zi ? bounds.max.z : bounds.min.z
                    };
                    Vector3 toCorner = Vector3Subtract(corner, framingTarget);
                    const float projRight = Vector3DotProduct(toCorner, right);
                    const float projUp = Vector3DotProduct(toCorner, camUp);
                    const float projForward = Vector3DotProduct(toCorner, forward);
                    minRight = std::min(minRight, projRight);
                    maxRight = std::max(maxRight, projRight);
                    minUp = std::min(minUp, projUp);
                    maxUp = std::max(maxUp, projUp);
                    minForward = std::min(minForward, projForward);
                    maxForward = std::max(maxForward, projForward);
                }
            }
        }

        const float leftBound = minRight * kHorizontalPadding;
        const float rightBound = maxRight * kHorizontalPadding;
        const float bottomBound = minUp * kBottomPadding;
        const float topBound = maxUp * kTopPadding;

        const float centerRight = (leftBound + rightBound) * 0.5f;
        const float centerUp = (bottomBound + topBound) * 0.5f;

        float orthoHalfSize = std::max({
            std::abs(leftBound - centerRight),
            std::abs(rightBound - centerRight),
            std::abs(bottomBound - centerUp),
            std::abs(topBound - centerUp)
        });
        if (orthoHalfSize <= 0.0f) {
            orthoHalfSize = static_cast<float>(renderSize) / 2.0f;
        }

        const float nearMargin = std::max(maxDim * 0.25f, 4.0f);
        const float maxDistance = std::max(nearMargin, 900.0f - std::max(0.0f, maxForward));
        const float camDistance = std::clamp(-minForward + nearMargin, nearMargin, maxDistance);
        const float targetOffsetX = centerRight;
        const float targetOffsetY = centerUp;
        const Vector3 cameraTarget = Vector3Add(
            framingTarget,
            Vector3Add(Vector3Scale(right, targetOffsetX), Vector3Scale(camUp, targetOffsetY)));
        camera.target = cameraTarget;
        camera.position = Vector3Add(cameraTarget, Vector3Scale(dir, camDistance));
        camera.fovy = orthoHalfSize * 2.0f;

        spdlog::trace(
            "Thumbnail renderer camera for {}: maxDim={}, camDistance={}, orthoHalfSize={}, focusY={}, offset=({}, {}), depth=[{}, {}], sc4Camera[yaw={}, pitchZoom{}={}]",
            tgi.ToString(), maxDim, camDistance, orthoHalfSize, framingTarget.y, targetOffsetX, targetOffsetY,
            minForward, maxForward, kSc4DefaultYawRadians, kThumbnailZoomIndex + 1,
            kSc4DefaultPitchRadians[kThumbnailZoomIndex]);

        BeginTextureMode(target);
        ClearBackground(BLANK);
        BeginMode3D(camera);
        rlDisableBackfaceCulling();
        DrawModelEx(modelHandle->model, Vector3Zero(), Vector3{0, 1, 0}, 0.0f,
                    Vector3One(), WHITE);
        rlEnableBackfaceCulling();
        EndMode3D();
        EndTextureMode();

        Image image = LoadImageFromTexture(target.texture);
        ImageFlipVertical(&image);
        ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

        Image finalImage{};
        if (kEnableSilhouettePostFit) {
            std::optional<AlphaBounds> visibleBounds = FindVisibleAlphaBounds(image, kAlphaFitThreshold);
            if (!visibleBounds.has_value()) {
                UnloadImage(image);
                UnloadRenderTexture(target);
                modelCache_.erase(tgi);
                return std::nullopt;
            }

            const int visibleWidth = visibleBounds->maxX - visibleBounds->minX + 1;
            const int visibleHeight = visibleBounds->maxY - visibleBounds->minY + 1;
            const int margin = std::max(1, static_cast<int>(std::ceil(std::max(visibleWidth, visibleHeight) * kPostFitMarginRatio)));
            Rectangle cropRect{
                static_cast<float>(std::max(0, visibleBounds->minX - margin)),
                static_cast<float>(std::max(0, visibleBounds->minY - margin)),
                static_cast<float>(std::min(static_cast<int>(renderSize), visibleBounds->maxX + margin + 1) -
                                   std::max(0, visibleBounds->minX - margin)),
                static_cast<float>(std::min(static_cast<int>(renderSize), visibleBounds->maxY + margin + 1) -
                                   std::max(0, visibleBounds->minY - margin))
            };

            Image cropped = ImageFromImage(image, cropRect);
            if (cropped.data == nullptr) {
                UnloadImage(image);
                UnloadRenderTexture(target);
                modelCache_.erase(tgi);
                return std::nullopt;
            }

            const float fitScale = std::min(
                static_cast<float>(size) / static_cast<float>(cropped.width),
                static_cast<float>(size) / static_cast<float>(cropped.height));
            const int fittedWidth = std::max(1, static_cast<int>(std::round(cropped.width * fitScale)));
            const int fittedHeight = std::max(1, static_cast<int>(std::round(cropped.height * fitScale)));

            ImageResize(&cropped, fittedWidth, fittedHeight);
            ImageFormat(&cropped, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

            finalImage = GenImageColor(static_cast<int>(size), static_cast<int>(size), BLANK);
            const Rectangle srcRect{
                0.0f,
                0.0f,
                static_cast<float>(cropped.width),
                static_cast<float>(cropped.height)
            };
            const Rectangle dstRect{
                std::floor((static_cast<float>(size) - cropped.width) * 0.5f),
                std::floor((static_cast<float>(size) - cropped.height) * 0.5f),
                static_cast<float>(cropped.width),
                static_cast<float>(cropped.height)
            };
            ImageDraw(&finalImage, cropped, srcRect, dstRect, WHITE);
            UnloadImage(cropped);
        }
        else {
            finalImage = ImageCopy(image);
            ImageResize(&finalImage, static_cast<int>(size), static_cast<int>(size));
            ImageFormat(&finalImage, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        }

        RenderedImage rendered;
        rendered.width = size;
        rendered.height = size;
        rendered.pixels.resize(size * size * 4);
        if (finalImage.data) {
            std::memcpy(rendered.pixels.data(), finalImage.data, rendered.pixels.size());
            for (size_t i = 0; i + 3 < rendered.pixels.size(); i += 4) {
                std::byte tmp = rendered.pixels[i];
                rendered.pixels[i] = rendered.pixels[i + 2];
                rendered.pixels[i + 2] = tmp;
            }
        }

        UnloadImage(finalImage);
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
