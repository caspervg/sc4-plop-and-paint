#include "TerrainDecalExamples.hpp"

namespace
{
    float TileOriginMeters(const int tileIndex, const float tileSizeMeters)
    {
        return static_cast<float>(tileIndex) * tileSizeMeters;
    }

    float TileCenterMeters(const int tileIndex, const float tileSizeMeters)
    {
        return (static_cast<float>(tileIndex) + 0.5f) * tileSizeMeters;
    }
}

namespace TerrainDecal
{
    std::vector<ClippedDecalDrawCommand> BuildExampleDrawCommands(const ExampleSceneOptions& options)
    {
        const float tile = options.tileSizeMeters;
        const float baseOriginX = TileOriginMeters(options.baseTileX, tile);
        const float baseOriginZ = TileOriginMeters(options.baseTileZ, tile);

        std::vector<ClippedDecalDrawCommand> commands;
        commands.reserve(3);

        // A centered half-tile stamp that should stay entirely inside one terrain cell.
        commands.push_back(ClippedDecalDrawCommand{
            .footprint =
                DecalFootprint{
                    .label = "HalfTileCenter",
                    .centerXMeters = TileCenterMeters(options.baseTileX, tile),
                    .centerZMeters = TileCenterMeters(options.baseTileZ, tile),
                    .widthMeters = tile * 0.5f,
                    .heightMeters = tile * 0.5f,
                    .rotationTurns = 0.0f,
                },
            .uvClip = UvClipRect{},
        });

        // A horizontally-biased stamp near the east edge of the neighboring tile.
        commands.push_back(ClippedDecalDrawCommand{
            .footprint =
                DecalFootprint{
                    .label = "EastEdgeOffset",
                    .centerXMeters = TileOriginMeters(options.baseTileX + 1, tile) + tile * 0.78f,
                    .centerZMeters = baseOriginZ + tile * 0.38f,
                    .widthMeters = tile * 0.75f,
                    .heightMeters = tile * 0.375f,
                    .rotationTurns = 0.0f,
                },
            .uvClip = UvClipRect{},
        });

        // A rotated stamp that intentionally crosses several tile boundaries.
        commands.push_back(ClippedDecalDrawCommand{
            .footprint =
                DecalFootprint{
                    .label = "DiagonalTwoTileSweep",
                    .centerXMeters = baseOriginX + tile * 1.4f,
                    .centerZMeters = baseOriginZ + tile * 1.2f,
                    .widthMeters = tile * 1.25f,
                    .heightMeters = tile * 0.625f,
                    .rotationTurns = 0.125f,
                },
            .uvClip = UvClipRect{},
        });

        return commands;
    }
}

