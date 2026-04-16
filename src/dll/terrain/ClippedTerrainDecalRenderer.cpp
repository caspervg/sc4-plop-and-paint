#include "ClippedTerrainDecalRenderer.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

#include "cISTETerrain.h"

namespace
{
    // SC4 uses primType 0 for caller-supplied explicit triangles.
    constexpr uint32_t kPrimTypeTriangleList = 0;
    constexpr uint32_t kTerrainVertexFormat = 0x0B;
    constexpr float kClipEpsilon = 1.0e-5f;

    struct TerrainDrawRect
    {
        int left;
        int top;
        int right;
        int bottom;
    };

    struct TerrainGridDimensions
    {
        int cellCountX = 0;
        int cellCountZ = 0;
        int vertexCountX = 0;
        int vertexCount = 0;
    };

    struct PackedTerrainVertex
    {
        float x;
        float y;
        float z;
        uint32_t diffuse;
        float u;
        float v;
        float extra0;
        float extra1;
    };

    static_assert(sizeof(PackedTerrainVertex) == 0x20,
                  "PackedTerrainVertex must match the game's 32-byte terrain vertex layout.");

    struct ClipVertex
    {
        PackedTerrainVertex vertex{};
        float clipU = 0.0f;
        float clipV = 0.0f;
    };

    struct OverlaySlotView
    {
        int32_t state = 0;
        uint32_t flags = 0;
        TerrainDrawRect rect{};
        const float* matrix = nullptr;
    };

    struct CellInfoEntry
    {
        int vertexIndex;
        uint32_t flatYBits;
    };

    struct RowTableEntry
    {
        const std::byte* data;
        uint32_t unknown1;
        uint32_t unknown2;
    };

    using DrawPrimsFn = void(__thiscall*)(SC4DrawContext*, uint32_t, uint32_t, uint32_t, const void*);

    [[nodiscard]] OverlaySlotView ReadOverlaySlotView(const std::byte* slotBase)
    {
        OverlaySlotView result{};
        if (!slotBase) {
            return result;
        }

        result.state = *reinterpret_cast<const int32_t*>(slotBase + 0x00);
        result.flags = *reinterpret_cast<const uint32_t*>(slotBase + 0x04);
        result.rect = *reinterpret_cast<const TerrainDrawRect*>(slotBase + 0x0C);
        result.matrix = reinterpret_cast<const float*>(slotBase + 0x1C);
        return result;
    }

    [[nodiscard]] bool ShouldClipU(const uint32_t flags) noexcept
    {
        if ((flags & 0x20u) != 0u) {
            return false;
        }

        return (flags & 0x2u) == 0u;
    }

    [[nodiscard]] bool ShouldClipV(const uint32_t flags) noexcept
    {
        return (flags & 0x20u) == 0u;
    }

    [[nodiscard]] float UnpackColorChannel(const uint32_t color, const int shift) noexcept
    {
        return static_cast<float>((color >> shift) & 0xFFu);
    }

    [[nodiscard]] uint32_t PackColor(const float a, const float r, const float g, const float b) noexcept
    {
        const auto clampByte = [](const float value) -> uint32_t {
            const float clamped = std::clamp(value, 0.0f, 255.0f);
            return static_cast<uint32_t>(std::lround(clamped));
        };

        return (clampByte(a) << 24u) |
               (clampByte(r) << 16u) |
               (clampByte(g) << 8u) |
               clampByte(b);
    }

    [[nodiscard]] PackedTerrainVertex LerpVertex(const PackedTerrainVertex& a,
                                                 const PackedTerrainVertex& b,
                                                 const float t) noexcept
    {
        PackedTerrainVertex out{};
        out.x = std::lerp(a.x, b.x, t);
        out.y = std::lerp(a.y, b.y, t);
        out.z = std::lerp(a.z, b.z, t);
        out.u = std::lerp(a.u, b.u, t);
        out.v = std::lerp(a.v, b.v, t);
        out.extra0 = std::lerp(a.extra0, b.extra0, t);
        out.extra1 = std::lerp(a.extra1, b.extra1, t);

        const float aA = UnpackColorChannel(a.diffuse, 24);
        const float aR = UnpackColorChannel(a.diffuse, 16);
        const float aG = UnpackColorChannel(a.diffuse, 8);
        const float aB = UnpackColorChannel(a.diffuse, 0);
        const float bA = UnpackColorChannel(b.diffuse, 24);
        const float bR = UnpackColorChannel(b.diffuse, 16);
        const float bG = UnpackColorChannel(b.diffuse, 8);
        const float bB = UnpackColorChannel(b.diffuse, 0);

        out.diffuse = PackColor(std::lerp(aA, bA, t),
                                std::lerp(aR, bR, t),
                                std::lerp(aG, bG, t),
                                std::lerp(aB, bB, t));
        return out;
    }

    void EvaluateFootprintUv(const float* matrix, ClipVertex& vertex) noexcept
    {
        if (!matrix) {
            return;
        }

        const float sourceX = vertex.vertex.x;
        const float sourceY = vertex.vertex.y;
        const float sourceZ = vertex.vertex.z;

        const float u = sourceX * matrix[0] + sourceY * matrix[4] + sourceZ * matrix[8] + matrix[12];
        const float v = sourceX * matrix[1] + sourceY * matrix[5] + sourceZ * matrix[9] + matrix[13];
        const float w = sourceX * matrix[3] + sourceY * matrix[7] + sourceZ * matrix[11] + matrix[15];

        if (std::fabs(w) > kClipEpsilon && std::fabs(w - 1.0f) > kClipEpsilon) {
            vertex.clipU = u / w;
            vertex.clipV = v / w;
        }
        else {
            vertex.clipU = u;
            vertex.clipV = v;
        }
    }

    [[nodiscard]] ClipVertex LerpClipVertex(const ClipVertex& a,
                                            const ClipVertex& b,
                                            const float t) noexcept
    {
        ClipVertex out{};
        out.vertex = LerpVertex(a.vertex, b.vertex, t);
        out.clipU = std::lerp(a.clipU, b.clipU, t);
        out.clipV = std::lerp(a.clipV, b.clipV, t);
        return out;
    }

    [[nodiscard]] bool IsInsidePlane(const ClipVertex& v,
                                     const bool useU,
                                     const bool isMinPlane,
                                     const float limit) noexcept
    {
        const float value = useU ? v.clipU : v.clipV;
        return isMinPlane ? value >= (limit - kClipEpsilon) : value <= (limit + kClipEpsilon);
    }

    [[nodiscard]] ClipVertex IntersectPlane(const ClipVertex& a,
                                            const ClipVertex& b,
                                            const bool useU,
                                            const float limit) noexcept
    {
        const float av = useU ? a.clipU : a.clipV;
        const float bv = useU ? b.clipU : b.clipV;
        const float denom = bv - av;
        const float t = std::fabs(denom) > kClipEpsilon ? (limit - av) / denom : 0.0f;
        return LerpClipVertex(a, b, std::clamp(t, 0.0f, 1.0f));
    }

    void ClipPolygonAgainstPlane(std::vector<ClipVertex>& polygon,
                                 const bool useU,
                                 const bool isMinPlane,
                                 const float limit)
    {
        if (polygon.empty()) {
            return;
        }

        std::vector<ClipVertex> output;
        output.reserve(polygon.size() + 2);

        ClipVertex previous = polygon.back();
        bool previousInside = IsInsidePlane(previous, useU, isMinPlane, limit);

        for (const auto& current : polygon) {
            const bool currentInside = IsInsidePlane(current, useU, isMinPlane, limit);

            if (currentInside != previousInside) {
                output.push_back(IntersectPlane(previous, current, useU, limit));
            }

            if (currentInside) {
                output.push_back(current);
            }

            previous = current;
            previousInside = currentInside;
        }

        polygon = std::move(output);
    }

    void EmitTriangleFan(const std::vector<ClipVertex>& polygon,
                         std::vector<PackedTerrainVertex>& output)
    {
        if (polygon.size() < 3) {
            return;
        }

        for (size_t i = 1; i + 1 < polygon.size(); ++i) {
            output.push_back(polygon[0].vertex);
            output.push_back(polygon[i].vertex);
            output.push_back(polygon[i + 1].vertex);
        }
    }

    void ClipAndEmitPolygon(std::vector<ClipVertex> polygon,
                             const bool clipU,
                             const bool clipV,
                             std::vector<PackedTerrainVertex>& output)
    {
        if (clipU) {
            ClipPolygonAgainstPlane(polygon, true, true, 0.0f);
            ClipPolygonAgainstPlane(polygon, true, false, 1.0f);
        }

        if (clipV) {
            ClipPolygonAgainstPlane(polygon, false, true, 0.0f);
            ClipPolygonAgainstPlane(polygon, false, false, 1.0f);
        }

        EmitTriangleFan(polygon, output);
    }

    [[nodiscard]] bool AllVerticesInside(const std::array<ClipVertex, 4>& vertices,
                                         const bool clipU,
                                         const bool clipV) noexcept
    {
        return std::all_of(vertices.begin(), vertices.end(), [clipU, clipV](const ClipVertex& vertex) {
            const bool insideU = !clipU || (vertex.clipU >= -kClipEpsilon && vertex.clipU <= 1.0f + kClipEpsilon);
            const bool insideV = !clipV || (vertex.clipV >= -kClipEpsilon && vertex.clipV <= 1.0f + kClipEpsilon);
            return insideU && insideV;
        });
    }

    [[nodiscard]] bool AnyVertexInside(const std::array<ClipVertex, 4>& vertices,
                                       const bool clipU,
                                       const bool clipV) noexcept
    {
        return std::any_of(vertices.begin(), vertices.end(), [clipU, clipV](const ClipVertex& vertex) {
            const bool insideU = !clipU || (vertex.clipU >= -kClipEpsilon && vertex.clipU <= 1.0f + kClipEpsilon);
            const bool insideV = !clipV || (vertex.clipV >= -kClipEpsilon && vertex.clipV <= 1.0f + kClipEpsilon);
            return insideU && insideV;
        });
    }

    [[nodiscard]] bool QuadMayIntersectClipBox(const std::array<ClipVertex, 4>& vertices,
                                               const bool clipU,
                                               const bool clipV) noexcept
    {
        if (AnyVertexInside(vertices, clipU, clipV)) {
            return true;
        }

        float minU = vertices[0].clipU;
        float maxU = vertices[0].clipU;
        float minV = vertices[0].clipV;
        float maxV = vertices[0].clipV;

        for (const auto& vertex : vertices) {
            minU = std::min(minU, vertex.clipU);
            maxU = std::max(maxU, vertex.clipU);
            minV = std::min(minV, vertex.clipV);
            maxV = std::max(maxV, vertex.clipV);
        }

        const bool overlapsU = !clipU || !(maxU < 0.0f || minU > 1.0f);
        const bool overlapsV = !clipV || !(maxV < 0.0f || minV > 1.0f);
        return overlapsU && overlapsV;
    }

    [[nodiscard]] const RowTableEntry* ReadRowTable(const uintptr_t globalAddress) noexcept
    {
        if (globalAddress == 0) {
            return nullptr;
        }

        return *reinterpret_cast<const RowTableEntry* const*>(globalAddress);
    }

    [[nodiscard]] const PackedTerrainVertex* GetTerrainVertexArray(const uintptr_t globalAddress) noexcept
    {
        if (globalAddress == 0) {
            return nullptr;
        }

        return *reinterpret_cast<const PackedTerrainVertex* const*>(globalAddress);
    }

    [[nodiscard]] const std::byte* GetPreparedCellVertexRow(const uintptr_t globalAddress, const int row) noexcept
    {
        if (globalAddress == 0 || row < 0) {
            return nullptr;
        }

        const auto* const rows = *reinterpret_cast<const RowTableEntry* const*>(globalAddress);
        if (!rows) {
            return nullptr;
        }

        return reinterpret_cast<const std::byte*>(rows[row].data);
    }

    [[nodiscard]] const RowTableEntry* GetPreparedCellVertexRowsTable(const uintptr_t globalAddress) noexcept
    {
        if (globalAddress == 0) {
            return nullptr;
        }

        return *reinterpret_cast<const RowTableEntry* const*>(globalAddress);
    }

    [[nodiscard]] const CellInfoEntry* GetCellInfoRow(const RowTableEntry* rows, const int row) noexcept
    {
        if (!rows || row < 0) {
            return nullptr;
        }

        return reinterpret_cast<const CellInfoEntry*>(rows[row].data);
    }

    [[nodiscard]] TerrainGridDimensions ReadTerrainGridDimensions(const TerrainDecal::HookAddresses& addresses) noexcept
    {
        TerrainGridDimensions result{};
        if (addresses.terrainCellCountXPtr != 0) {
            result.cellCountX = *reinterpret_cast<const int*>(addresses.terrainCellCountXPtr);
        }

        if (addresses.terrainCellCountZPtr != 0) {
            result.cellCountZ = *reinterpret_cast<const int*>(addresses.terrainCellCountZPtr);
        }

        if (addresses.terrainVertexCountXPtr != 0) {
            result.vertexCountX = *reinterpret_cast<const int*>(addresses.terrainVertexCountXPtr);
        }

        if (addresses.terrainVertexCountPtr != 0) {
            result.vertexCount = *reinterpret_cast<const int*>(addresses.terrainVertexCountPtr);
        }

        return result;
    }

    [[nodiscard]] TerrainDrawRect ClampTerrainDrawRect(const TerrainDrawRect& rect,
                                                       const TerrainGridDimensions& dimensions) noexcept
    {
        TerrainDrawRect result{};
        result.left = std::clamp(rect.left, 0, std::max(0, dimensions.cellCountX));
        result.top = std::clamp(rect.top, 0, std::max(0, dimensions.cellCountZ));
        result.right = std::clamp(rect.right, result.left, std::max(0, dimensions.cellCountX));
        result.bottom = std::clamp(rect.bottom, result.top, std::max(0, dimensions.cellCountZ));
        return result;
    }

    [[nodiscard]] bool LoadTerrainCellVertices(const TerrainDecal::HookAddresses& addresses,
                                               const int cellX,
                                               const int cellZ,
                                               std::array<PackedTerrainVertex, 4>& result)
    {
        const TerrainGridDimensions dimensions = ReadTerrainGridDimensions(addresses);
        if (dimensions.cellCountX <= 0 || dimensions.cellCountZ <= 0 ||
            dimensions.vertexCountX <= 0 || dimensions.vertexCount <= 0) {
            return false;
        }

        if (cellX >= dimensions.cellCountX || cellZ >= dimensions.cellCountZ) {
            return false;
        }

        const auto* const preparedRow = GetPreparedCellVertexRow(addresses.terrainPreparedCellVerticesRowsPtr, cellZ);
        if (preparedRow) {
            const size_t rowOffset = static_cast<size_t>(cellX) * sizeof(PackedTerrainVertex) * 4;
            const auto* const preparedVertices =
                reinterpret_cast<const PackedTerrainVertex*>(preparedRow + rowOffset);

            result[0] = preparedVertices[0];
            result[1] = preparedVertices[1];
            result[2] = preparedVertices[2];
            result[3] = preparedVertices[3];
            return true;
        }

        const auto* const vertices = GetTerrainVertexArray(addresses.terrainGridVerticesPtr);
        const auto* const rows = ReadRowTable(addresses.terrainCellInfoRowsPtr);
        if (!vertices) {
            return false;
        }
        if (!rows) {
            return false;
        }

        const int vertexCountX = dimensions.vertexCountX;
        int rowRelativeIndex = cellX;
        const auto* const row = GetCellInfoRow(rows, cellZ);
        if (row && cellX >= 0) {
            rowRelativeIndex = row[cellX].vertexIndex;
        }
        if (rowRelativeIndex < 0) {
            return false;
        }

        const int baseIndex = cellZ * vertexCountX + rowRelativeIndex;
        if (baseIndex + vertexCountX + 1 >= dimensions.vertexCount) {
            return false;
        }

        result[0] = vertices[baseIndex];
        result[1] = vertices[baseIndex + vertexCountX];
        result[2] = vertices[baseIndex + vertexCountX + 1];
        result[3] = vertices[baseIndex + 1];

        if (row) {
            const float flatY = std::bit_cast<float>(row[cellX].flatYBits);
            result[0].y = flatY;
            result[1].y = flatY;
            result[2].y = flatY;
            result[3].y = flatY;
        }

        return true;
    }
}

namespace TerrainDecal
{
    ClippedTerrainDecalRenderer::ClippedTerrainDecalRenderer(const RendererOptions options)
        : options_(options)
    {
    }

    void ClippedTerrainDecalRenderer::SetOptions(const RendererOptions& options) noexcept
    {
        options_ = options;
    }

    const RendererOptions& ClippedTerrainDecalRenderer::GetOptions() const noexcept
    {
        return options_;
    }

    DrawResult ClippedTerrainDecalRenderer::Draw(const DrawRequest& request)
    {
        if (!options_.enableClippedRendering) {
            return DrawResult::FallThroughToVanilla;
        }

        if (!request.addresses || !request.terrain || !request.overlaySlotBase || !request.drawContext) {
            return DrawResult::FallThroughToVanilla;
        }

        const OverlaySlotView slot = ReadOverlaySlotView(request.overlaySlotBase);
        if (slot.state != -1 || !slot.matrix) {
            return DrawResult::FallThroughToVanilla;
        }

        const bool clipU = ShouldClipU(slot.flags);
        const bool clipV = ShouldClipV(slot.flags);
        if (!clipU && !clipV) {
            return DrawResult::FallThroughToVanilla;
        }

        if (slot.rect.left >= slot.rect.right || slot.rect.top >= slot.rect.bottom) {
            return DrawResult::Handled;
        }

        const TerrainGridDimensions dimensions = ReadTerrainGridDimensions(*request.addresses);
        if (dimensions.cellCountX <= 0 || dimensions.cellCountZ <= 0) {
            return DrawResult::Handled;
        }

        const TerrainDrawRect drawRect = ClampTerrainDrawRect(slot.rect, dimensions);
        if (drawRect.left >= drawRect.right || drawRect.top >= drawRect.bottom) {
            return DrawResult::Handled;
        }

        std::vector<PackedTerrainVertex> outputVertices;
        bool loadedAnyTerrainCells = false;
        const int cellCount = std::max(0, drawRect.right - drawRect.left) * std::max(0, drawRect.bottom - drawRect.top);
        outputVertices.reserve(static_cast<size_t>(cellCount) * 12);

        for (int cellZ = drawRect.top; cellZ < drawRect.bottom; ++cellZ) {
            for (int cellX = drawRect.left; cellX < drawRect.right; ++cellX) {
                std::array<ClipVertex, 4> vertices{};
                std::array<PackedTerrainVertex, 4> sourceVertices{};
                if (!LoadTerrainCellVertices(*request.addresses, cellX, cellZ, sourceVertices)) {
                    continue;
                }

                loadedAnyTerrainCells = true;

                for (size_t i = 0; i < sourceVertices.size(); ++i) {
                    vertices[i].vertex = sourceVertices[i];
                }

                for (auto& vertex : vertices) {
                    EvaluateFootprintUv(slot.matrix, vertex);
                }

                if (!QuadMayIntersectClipBox(vertices, clipU, clipV)) {
                    continue;
                }

                if (AllVerticesInside(vertices, clipU, clipV)) {
                    outputVertices.push_back(vertices[0].vertex);
                    outputVertices.push_back(vertices[1].vertex);
                    outputVertices.push_back(vertices[2].vertex);
                    outputVertices.push_back(vertices[0].vertex);
                    outputVertices.push_back(vertices[2].vertex);
                    outputVertices.push_back(vertices[3].vertex);
                }
                else {
                    ClipAndEmitPolygon({vertices[0], vertices[1], vertices[2], vertices[3]},
                                       clipU,
                                       clipV,
                                       outputVertices);
                }
            }
        }

        if (outputVertices.empty()) {
            if (!loadedAnyTerrainCells) {
                return DrawResult::FallThroughToVanilla;
            }

            return DrawResult::Handled;
        }

        const auto drawPrims = reinterpret_cast<DrawPrimsFn>(request.addresses->drawPrims);
        drawPrims(request.drawContext,
                  kPrimTypeTriangleList,
                  kTerrainVertexFormat,
                  static_cast<uint32_t>(outputVertices.size()),
                  outputVertices.data());

        return DrawResult::Handled;
    }
}
