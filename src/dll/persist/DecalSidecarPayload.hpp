#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace PlopAndPaint::Sidecar
{
    struct WorldPos
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct TextureKey
    {
        uint32_t type = 0;
        uint32_t group = 0;
        uint32_t instance = 0;
    };

    // Mirrors TerrainDecal::OverlayUvMode one-to-one. Duplicated here so the
    // payload layer has no dependency on the renderer internals and can evolve
    // independently. Translation helpers live in DecalSidecarRepository.
    enum class UvMode : uint32_t
    {
        StretchSubrect = 0,
        ClipSubrect = 1,
    };

    struct UvSubrect
    {
        float u1 = 0.0f;
        float v1 = 0.0f;
        float u2 = 1.0f;
        float v2 = 1.0f;
        UvMode mode = UvMode::StretchSubrect;
    };

    // Snapshot of cISTEOverlayManager::cDecalInfo. Seven floats in their native
    // order so that older DLL versions can re-emit the blob untouched even if
    // they don't understand its contents.
    struct DecalInfoSnapshot
    {
        float centerU = 0.5f;
        float centerV = 0.5f;
        float radiusU = 0.5f;
        float radiusV = 0.5f;
        float rotationRadians = 0.0f;
        float scaleX = 1.0f;
        float scaleY = 1.0f;
    };

    // Forward-compat escape hatch: fields we read but don't understand. Writers
    // preserve these untouched so round-tripping through an older DLL is lossless.
    struct UnknownField
    {
        uint32_t tag = 0;
        std::vector<uint8_t> bytes;
    };

    struct DecalEntry
    {
        WorldPos worldPos{};
        std::optional<TextureKey> textureKey;
        std::optional<UvSubrect> uvSubrect;
        std::optional<DecalInfoSnapshot> decalInfo;
        std::optional<float> opacity;
        std::optional<uint32_t> userFlags;
        std::vector<UnknownField> unknownFields;
    };

    struct UnknownChunk
    {
        uint32_t tag = 0;
        std::vector<uint8_t> bytes;
    };

    struct SidecarDocument
    {
        uint16_t versionMajor = 1;
        uint16_t versionMinor = 0;
        uint32_t flags = 0;
        std::vector<DecalEntry> decals;
        std::vector<UnknownChunk> unknownChunks;
    };
}
