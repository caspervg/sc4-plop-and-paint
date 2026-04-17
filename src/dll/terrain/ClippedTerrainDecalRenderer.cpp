#include "ClippedTerrainDecalRenderer.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

#include "cISTETerrain.h"
#include "utils/Logger.h"

namespace
{
    // SC4 uses primType 0 for caller-supplied explicit triangles.
    constexpr uint32_t kPrimTypeTriangleList = 0;
    constexpr uint32_t kTerrainVertexFormat = 0x0B;
    constexpr float kClipEpsilon = 1.0e-5f;
    using SetTexTransform4Fn = void(__thiscall*)(SC4DrawContext*, const float*, int);

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

    struct TextureTransformOverride
    {
        std::array<float, 16> adjusted{};
        bool active = false;
    };

    struct ClipBounds
    {
        float minU = 0.0f;
        float maxU = 1.0f;
        float minV = 0.0f;
        float maxV = 1.0f;
    };

    [[nodiscard]] uint32_t NormalizeOverlayIdKey(const uint32_t overlayId) noexcept
    {
        return overlayId & 0x7FFFFFFFu;
    }

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

    [[nodiscard]] bool IsValidUvSubrect(const TerrainDecal::OverlayUvSubrect& uvRect) noexcept
    {
        return std::isfinite(uvRect.u1) &&
               std::isfinite(uvRect.v1) &&
               std::isfinite(uvRect.u2) &&
               std::isfinite(uvRect.v2) &&
               uvRect.u1 < uvRect.u2 &&
               uvRect.v1 < uvRect.v2;
    }

    [[nodiscard]] const char* DescribeOverlayUvMode(const TerrainDecal::OverlayUvMode mode) noexcept
    {
        switch (mode) {
        case TerrainDecal::OverlayUvMode::StretchSubrect:
            return "stretch";
        case TerrainDecal::OverlayUvMode::ClipSubrect:
            return "clip";
        default:
            return "unknown";
        }
    }

    [[nodiscard]] bool TryResolveOverlayId(const TerrainDecal::DrawRequest& request, uint32_t& overlayId) noexcept
    {
        overlayId = 0;

        if (!request.addresses ||
            !request.overlayManager ||
            !request.overlaySlotBase ||
            request.addresses->overlaySlotsPtrOffset <= 0 ||
            request.addresses->overlaySlotStride <= 0) {
            return false;
        }

        const auto* const overlayManagerBytes = reinterpret_cast<const std::byte*>(request.overlayManager);
        const auto* const slotsBase =
            *reinterpret_cast<const std::byte* const*>(overlayManagerBytes + request.addresses->overlaySlotsPtrOffset);
        if (!slotsBase) {
            return false;
        }

        const auto slotAddress = reinterpret_cast<uintptr_t>(request.overlaySlotBase);
        const auto slotsBaseAddress = reinterpret_cast<uintptr_t>(slotsBase);
        if (slotAddress < slotsBaseAddress) {
            return false;
        }

        const std::ptrdiff_t delta = static_cast<std::ptrdiff_t>(slotAddress - slotsBaseAddress);
        if ((delta % request.addresses->overlaySlotStride) != 0) {
            return false;
        }

        const auto slotIndex = delta / request.addresses->overlaySlotStride;
        if (slotIndex < 0) {
            return false;
        }

        overlayId = static_cast<uint32_t>(slotIndex);
        return true;
    }

    [[nodiscard]] TextureTransformOverride BuildTextureTransformOverride(
        const float* baseTransform,
        const TerrainDecal::OverlayUvSubrect& uvRect) noexcept
    {
        TextureTransformOverride result{};
        if (!baseTransform || !IsValidUvSubrect(uvRect)) {
            return result;
        }

        result.adjusted = {
            baseTransform[0],  baseTransform[1],  baseTransform[2],  baseTransform[3],
            baseTransform[4],  baseTransform[5],  baseTransform[6],  baseTransform[7],
            baseTransform[8],  baseTransform[9],  baseTransform[10], baseTransform[11],
            baseTransform[12], baseTransform[13], baseTransform[14], baseTransform[15]
        };

        const float width = uvRect.u2 - uvRect.u1;
        const float height = uvRect.v2 - uvRect.v1;
        constexpr int uColumn[] = {0, 4, 8, 12};
        constexpr int vColumn[] = {1, 5, 9, 13};
        constexpr int wColumn[] = {3, 7, 11, 15};

        for (size_t i = 0; i < std::size(uColumn); ++i) {
            result.adjusted[uColumn[i]] = width * baseTransform[uColumn[i]] +
                                          uvRect.u1 * baseTransform[wColumn[i]];
            result.adjusted[vColumn[i]] = height * baseTransform[vColumn[i]] +
                                          uvRect.v1 * baseTransform[wColumn[i]];
        }

        result.active = true;
        return result;
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
                             const ClipBounds& bounds,
                             std::vector<PackedTerrainVertex>& output)
    {
        if (clipU) {
            ClipPolygonAgainstPlane(polygon, true, true, bounds.minU);
            ClipPolygonAgainstPlane(polygon, true, false, bounds.maxU);
        }

        if (clipV) {
            ClipPolygonAgainstPlane(polygon, false, true, bounds.minV);
            ClipPolygonAgainstPlane(polygon, false, false, bounds.maxV);
        }

        EmitTriangleFan(polygon, output);
    }

    [[nodiscard]] bool AllVerticesInside(const std::array<ClipVertex, 4>& vertices,
                                         const bool clipU,
                                         const bool clipV,
                                         const ClipBounds& bounds) noexcept
    {
        return std::all_of(vertices.begin(), vertices.end(), [clipU, clipV, bounds](const ClipVertex& vertex) {
            const bool insideU = !clipU || (vertex.clipU >= bounds.minU - kClipEpsilon &&
                                            vertex.clipU <= bounds.maxU + kClipEpsilon);
            const bool insideV = !clipV || (vertex.clipV >= bounds.minV - kClipEpsilon &&
                                            vertex.clipV <= bounds.maxV + kClipEpsilon);
            return insideU && insideV;
        });
    }

    [[nodiscard]] bool AnyVertexInside(const std::array<ClipVertex, 4>& vertices,
                                       const bool clipU,
                                       const bool clipV,
                                       const ClipBounds& bounds) noexcept
    {
        return std::any_of(vertices.begin(), vertices.end(), [clipU, clipV, bounds](const ClipVertex& vertex) {
            const bool insideU = !clipU || (vertex.clipU >= bounds.minU - kClipEpsilon &&
                                            vertex.clipU <= bounds.maxU + kClipEpsilon);
            const bool insideV = !clipV || (vertex.clipV >= bounds.minV - kClipEpsilon &&
                                            vertex.clipV <= bounds.maxV + kClipEpsilon);
            return insideU && insideV;
        });
    }

    [[nodiscard]] bool QuadMayIntersectClipBox(const std::array<ClipVertex, 4>& vertices,
                                               const bool clipU,
                                               const bool clipV,
                                               const ClipBounds& bounds) noexcept
    {
        if (AnyVertexInside(vertices, clipU, clipV, bounds)) {
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

        const bool overlapsU = !clipU || !(maxU < bounds.minU || minU > bounds.maxU);
        const bool overlapsV = !clipV || !(maxV < bounds.minV || minV > bounds.maxV);
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

    void ClippedTerrainDecalRenderer::SetOverlayUvSubrect(const uint32_t overlayId, const OverlayUvSubrect& uvRect)
    {
        const uint32_t normalizedOverlayId = NormalizeOverlayIdKey(overlayId);
        overlayUvSubrects_[normalizedOverlayId] = uvRect;
        LOG_INFO("TerrainDecalRenderer: registered UV override for overlay {} (normalized {}, mode={}) -> [{:.3f}, {:.3f}] to [{:.3f}, {:.3f}]",
                 overlayId,
                 normalizedOverlayId,
                 DescribeOverlayUvMode(uvRect.mode),
                 uvRect.u1,
                 uvRect.v1,
                 uvRect.u2,
                 uvRect.v2);
    }

    bool ClippedTerrainDecalRenderer::RemoveOverlayUvSubrect(const uint32_t overlayId) noexcept
    {
        const uint32_t normalizedOverlayId = NormalizeOverlayIdKey(overlayId);
        const bool removed = overlayUvSubrects_.erase(normalizedOverlayId) > 0;
        if (removed) {
            LOG_INFO("TerrainDecalRenderer: removed UV override for overlay {} (normalized {})",
                     overlayId,
                     normalizedOverlayId);
        }
        return removed;
    }

    void ClippedTerrainDecalRenderer::ClearOverlayUvSubrects() noexcept
    {
        if (!overlayUvSubrects_.empty()) {
            LOG_INFO("TerrainDecalRenderer: cleared {} UV override entries", overlayUvSubrects_.size());
        }
        overlayUvSubrects_.clear();
    }

    bool ClippedTerrainDecalRenderer::TryGetOverlayUvSubrect(const uint32_t overlayId,
                                                             OverlayUvSubrect& uvRect) const noexcept
    {
        const uint32_t normalizedOverlayId = NormalizeOverlayIdKey(overlayId);
        const auto it = overlayUvSubrects_.find(normalizedOverlayId);
        if (it == overlayUvSubrects_.end()) {
            return false;
        }

        uvRect = it->second;
        return true;
    }

    DrawResult ClippedTerrainDecalRenderer::Draw(const DrawRequest& request)
    {
        const bool debugOverridesActive = !overlayUvSubrects_.empty();

        if (!options_.enableClippedRendering) {
            return DrawResult::FallThroughToVanilla;
        }

        if (!request.addresses || !request.terrain || !request.overlaySlotBase || !request.drawContext) {
            if (debugOverridesActive) {
                LOG_WARN("TerrainDecalRenderer: falling through before draw because request is incomplete "
                         "(addresses={}, terrain={}, slotBase={}, drawContext={})",
                         request.addresses != nullptr,
                         request.terrain != nullptr,
                         request.overlaySlotBase != nullptr,
                         request.drawContext != nullptr);
            }
            return DrawResult::FallThroughToVanilla;
        }

        const OverlaySlotView slot = ReadOverlaySlotView(request.overlaySlotBase);
        if (slot.state != -1 || !slot.matrix) {
            if (debugOverridesActive) {
                LOG_WARN("TerrainDecalRenderer: falling through because slot state/matrix is invalid "
                         "(state={}, matrix={})",
                         slot.state,
                         static_cast<const void*>(slot.matrix));
            }
            return DrawResult::FallThroughToVanilla;
        }

        const bool clipU = ShouldClipU(slot.flags);
        const bool clipV = ShouldClipV(slot.flags);
        uint32_t overlayId = 0;
        OverlayUvSubrect uvRect{};
        const bool hasOverlayId = TryResolveOverlayId(request, overlayId);
        const bool hasUvOverride = hasOverlayId && TryGetOverlayUvSubrect(overlayId, uvRect);
        const bool clipOnlyUvOverride = hasUvOverride && uvRect.mode == OverlayUvMode::ClipSubrect;
        const bool effectiveClipU = clipU || clipOnlyUvOverride;
        const bool effectiveClipV = clipV || clipOnlyUvOverride;
        const ClipBounds clipBounds = clipOnlyUvOverride
                                          ? ClipBounds{.minU = uvRect.u1, .maxU = uvRect.u2, .minV = uvRect.v1, .maxV = uvRect.v2}
                                          : ClipBounds{};
        if (debugOverridesActive) {
            LOG_INFO("TerrainDecalRenderer: draw slotBase={} resolvedOverlayId={} overlayId={} hasUvOverride={} flags=0x{:08X} clipU={} clipV={} effectiveClipU={} effectiveClipV={}",
                     static_cast<const void*>(request.overlaySlotBase),
                     hasOverlayId,
                     overlayId,
                     hasUvOverride,
                     slot.flags,
                     clipU,
                     clipV,
                     effectiveClipU,
                     effectiveClipV);
        }
        if (!effectiveClipU && !effectiveClipV && !hasUvOverride) {
            return DrawResult::FallThroughToVanilla;
        }

        const float* const baseTexTransform = request.activeTexTransform ? request.activeTexTransform : slot.matrix;
        const TextureTransformOverride texTransformOverride =
            (hasUvOverride && uvRect.mode == OverlayUvMode::StretchSubrect)
                ? BuildTextureTransformOverride(baseTexTransform, uvRect)
                : TextureTransformOverride{};
        if (hasUvOverride && uvRect.mode == OverlayUvMode::StretchSubrect && !texTransformOverride.active) {
            LOG_WARN("TerrainDecalRenderer: UV override exists for overlay {} but transform override could not be built",
                     overlayId);
            return DrawResult::FallThroughToVanilla;
        }
        if (hasUvOverride) {
            LOG_INFO("TerrainDecalRenderer: overlay {} UV override mode={} active [{:.3f}, {:.3f}] to [{:.3f}, {:.3f}] using tex stage {}",
                     overlayId,
                     DescribeOverlayUvMode(uvRect.mode),
                     uvRect.u1,
                     uvRect.v1,
                     uvRect.u2,
                     uvRect.v2,
                     request.activeTexTransformStage);
        }
        if (hasUvOverride &&
            uvRect.mode == OverlayUvMode::StretchSubrect &&
            request.activeTexTransformStage < 0) {
            LOG_WARN("TerrainDecalRenderer: overlay {} UV override requested but no active texture transform stage was captured",
                     overlayId);
            return DrawResult::FallThroughToVanilla;
        }

        if (slot.rect.left >= slot.rect.right || slot.rect.top >= slot.rect.bottom) {
            if (hasUvOverride || debugOverridesActive) {
                LOG_INFO("TerrainDecalRenderer: overlay {} handled as empty rect [{},{}]-[{},{}]",
                         overlayId,
                         slot.rect.left,
                         slot.rect.top,
                         slot.rect.right,
                         slot.rect.bottom);
            }
            return DrawResult::Handled;
        }

        const TerrainGridDimensions dimensions = ReadTerrainGridDimensions(*request.addresses);
        if (dimensions.cellCountX <= 0 || dimensions.cellCountZ <= 0) {
            if (hasUvOverride || debugOverridesActive) {
                LOG_WARN("TerrainDecalRenderer: overlay {} handled with invalid terrain dimensions {}x{}",
                         overlayId,
                         dimensions.cellCountX,
                         dimensions.cellCountZ);
            }
            return DrawResult::Handled;
        }

        const TerrainDrawRect drawRect = ClampTerrainDrawRect(slot.rect, dimensions);
        if (drawRect.left >= drawRect.right || drawRect.top >= drawRect.bottom) {
            if (hasUvOverride || debugOverridesActive) {
                LOG_INFO("TerrainDecalRenderer: overlay {} handled with empty clamped rect [{},{}]-[{},{}]",
                         overlayId,
                         drawRect.left,
                         drawRect.top,
                         drawRect.right,
                         drawRect.bottom);
            }
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

                if (!QuadMayIntersectClipBox(vertices, effectiveClipU, effectiveClipV, clipBounds)) {
                    continue;
                }

                if (AllVerticesInside(vertices, effectiveClipU, effectiveClipV, clipBounds)) {
                    outputVertices.push_back(vertices[0].vertex);
                    outputVertices.push_back(vertices[1].vertex);
                    outputVertices.push_back(vertices[2].vertex);
                    outputVertices.push_back(vertices[0].vertex);
                    outputVertices.push_back(vertices[2].vertex);
                    outputVertices.push_back(vertices[3].vertex);
                }
                else {
                    ClipAndEmitPolygon({vertices[0], vertices[1], vertices[2], vertices[3]},
                                       effectiveClipU,
                                       effectiveClipV,
                                       clipBounds,
                                       outputVertices);
                }
            }
        }

        if (outputVertices.empty()) {
            if (!loadedAnyTerrainCells) {
                if (hasUvOverride || debugOverridesActive) {
                    LOG_WARN("TerrainDecalRenderer: overlay {} fell through because no terrain cells loaded", overlayId);
                }
                return DrawResult::FallThroughToVanilla;
            }

            if (hasUvOverride || debugOverridesActive) {
                LOG_INFO("TerrainDecalRenderer: overlay {} handled but produced no output vertices", overlayId);
            }
            return DrawResult::Handled;
        }

        if (texTransformOverride.active) {
            const auto setTexTransform = reinterpret_cast<SetTexTransform4Fn>(request.addresses->setTexTransform4);
            setTexTransform(request.drawContext, texTransformOverride.adjusted.data(), request.activeTexTransformStage);
        }

        const auto drawPrims = reinterpret_cast<DrawPrimsFn>(request.addresses->drawPrims);
        drawPrims(request.drawContext,
                  kPrimTypeTriangleList,
                  kTerrainVertexFormat,
                  static_cast<uint32_t>(outputVertices.size()),
                  outputVertices.data());
        if (hasUvOverride || debugOverridesActive) {
            LOG_INFO("TerrainDecalRenderer: overlay {} submitted {} vertices", overlayId, outputVertices.size());
        }

        if (texTransformOverride.active) {
            const auto setTexTransform = reinterpret_cast<SetTexTransform4Fn>(request.addresses->setTexTransform4);
            setTexTransform(request.drawContext, baseTexTransform, request.activeTexTransformStage);
        }

        return DrawResult::Handled;
    }
}
