#include "TerrainDecalSymbols.hpp"

namespace TerrainDecal
{
    std::optional<HookAddresses> ResolveHookAddresses(const uint16_t gameVersion)
    {
        switch (gameVersion) {
        case 641:
            // Observed in the currently loaded Windows x86 1.1.641 executable.
            return HookAddresses{
                .gameVersion = 641,
                .drawDecals = 0x00736790u,
                .drawRect = 0x00735720u,
                .drawRectCallSite = 0x00736B88u,
                .setVertexBuffer = 0x007D2970u,
                .drawPrims = 0x007D2990u,
                .drawPrimsIndexed = 0x007D29C0u,
                .drawPrimsIndexedRaw = 0x007D29F0u,
                .drawTerrainMeshSubsetInDrawFrustum = 0x007541C0u,
                .drawTerrainMeshSubsetWithVertBufExtensionInDrawFrustum = 0x007545C0u,
                .terrainGridVerticesPtr = 0x00B4C758u,
                .terrainCellInfoRowsPtr = 0x00B4C6ACu,
                .terrainVertexCountXPtr = 0x00B4C74Cu,
                .overlayRectOffset = 0x0C,
            };
        default:
            return std::nullopt;
        }
    }

    std::string_view DescribeKnownAddressSet(const uint16_t gameVersion) noexcept
    {
        switch (gameVersion) {
        case 641:
            return "SimCity 4 1.1.641 Windows x86";
        default:
            return "unknown";
        }
    }
}
