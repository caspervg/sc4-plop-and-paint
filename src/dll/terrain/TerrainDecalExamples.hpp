#pragma once

#include <string_view>
#include <vector>

namespace TerrainDecal
{
    struct UvClipRect
    {
        bool clipU = true;
        bool clipV = true;
        float uMin = 0.0f;
        float uMax = 1.0f;
        float vMin = 0.0f;
        float vMax = 1.0f;
    };

    struct DecalFootprint
    {
        std::string_view label;
        float centerXMeters = 0.0f;
        float centerZMeters = 0.0f;
        float widthMeters = 16.0f;
        float heightMeters = 16.0f;
        float rotationTurns = 0.0f;
    };

    struct ClippedDecalDrawCommand
    {
        DecalFootprint footprint;
        UvClipRect uvClip;
    };

    struct ExampleSceneOptions
    {
        int baseTileX = 0;
        int baseTileZ = 0;
        float tileSizeMeters = 16.0f;
    };

    [[nodiscard]] std::vector<ClippedDecalDrawCommand> BuildExampleDrawCommands(
        const ExampleSceneOptions& options = {});
}

