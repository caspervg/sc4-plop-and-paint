#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace TerrainDecal
{
    struct HookAddresses
    {
        uint16_t gameVersion = 0;
        uintptr_t drawDecals = 0;
        uintptr_t drawRect = 0;
        uintptr_t drawRectCallSite = 0;
        uintptr_t setTexTransform4 = 0;
        uintptr_t setTexTransform4CallSite = 0;
        uintptr_t setVertexBuffer = 0;
        uintptr_t drawPrims = 0;
        uintptr_t drawPrimsIndexed = 0;
        uintptr_t drawPrimsIndexedRaw = 0;
        uintptr_t drawTerrainMeshSubsetInDrawFrustum = 0;
        uintptr_t drawTerrainMeshSubsetWithVertBufExtensionInDrawFrustum = 0;
        uintptr_t terrainGridVerticesPtr = 0;
        uintptr_t terrainCellInfoRowsPtr = 0;
        uintptr_t terrainPreparedCellVerticesRowsPtr = 0;
        uintptr_t terrainCellCountXPtr = 0;
        uintptr_t terrainCellCountZPtr = 0;
        uintptr_t terrainVertexCountXPtr = 0;
        uintptr_t terrainVertexCountPtr = 0;
        std::ptrdiff_t overlayRectOffset = 0;
        std::ptrdiff_t overlaySlotsPtrOffset = 0;
        std::ptrdiff_t overlaySlotStride = 0;
    };

    [[nodiscard]] std::optional<HookAddresses> ResolveHookAddresses(uint16_t gameVersion);
    [[nodiscard]] std::string_view DescribeKnownAddressSet(uint16_t gameVersion) noexcept;
}
