#include "PolygonPlacer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <random>

#include "../common/WeightedPicker.hpp"
#include "cISTETerrain.h"

namespace {
    constexpr float kEpsilon = 1e-6f;

    struct XZPoint {
        float x;
        float z;
    };

    float Cross2D(const XZPoint& a, const XZPoint& b, const XZPoint& c) {
        return (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
    }

    float PolygonAreaSignedXZ(const std::vector<cS3DVector3>& vertices) {
        float area2 = 0.0f;
        const size_t n = vertices.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            area2 += vertices[j].fX * vertices[i].fZ - vertices[i].fX * vertices[j].fZ;
        }
        return area2 * 0.5f;
    }

    uint32_t HashPatchCell_(const int x, const int z, const uint32_t seed) {
        uint32_t value = seed;
        value ^= static_cast<uint32_t>(x) * 0x9E3779B9u;
        value ^= static_cast<uint32_t>(z) * 0x85EBCA6Bu;
        value ^= value >> 16;
        value *= 0x7FEB352Du;
        value ^= value >> 15;
        value *= 0x846CA68Bu;
        value ^= value >> 16;
        return value;
    }

    float PatchNoise_(const float x, const float z, const float patchCellSize, const uint32_t seed) {
        if (patchCellSize <= kEpsilon) {
            return 0.5f;
        }

        const int patchX = static_cast<int>(std::floor(x / patchCellSize));
        const int patchZ = static_cast<int>(std::floor(z / patchCellSize));
        return static_cast<float>(HashPatchCell_(patchX, patchZ, seed)) / 4294967295.0f;
    }

    bool PointInTriangleXZ(const XZPoint& p, const XZPoint& a, const XZPoint& b, const XZPoint& c) {
        const float c1 = Cross2D(p, a, b);
        const float c2 = Cross2D(p, b, c);
        const float c3 = Cross2D(p, c, a);

        const bool hasNeg = (c1 < -kEpsilon) || (c2 < -kEpsilon) || (c3 < -kEpsilon);
        const bool hasPos = (c1 > kEpsilon) || (c2 > kEpsilon) || (c3 > kEpsilon);
        return !(hasNeg && hasPos);
    }

    struct TriangleIndices {
        size_t a = 0;
        size_t b = 0;
        size_t c = 0;
    };

    bool TriangulatePolygon_(const std::vector<cS3DVector3>& vertices, std::vector<TriangleIndices>& triangles) {
        triangles.clear();
        if (vertices.size() < 3) {
            return false;
        }

        std::vector<size_t> indices(vertices.size());
        for (size_t i = 0; i < indices.size(); ++i) {
            indices[i] = i;
        }

        const bool ccw = PolygonAreaSignedXZ(vertices) > 0.0f;
        size_t safety = 0;
        while (indices.size() > 3 && safety < vertices.size() * vertices.size()) {
            bool earClipped = false;
            const size_t m = indices.size();

            for (size_t i = 0; i < m; ++i) {
                const size_t prev = indices[(i + m - 1) % m];
                const size_t curr = indices[i];
                const size_t next = indices[(i + 1) % m];

                const XZPoint a{vertices[prev].fX, vertices[prev].fZ};
                const XZPoint b{vertices[curr].fX, vertices[curr].fZ};
                const XZPoint c{vertices[next].fX, vertices[next].fZ};
                const float cross = Cross2D(a, b, c);
                if (ccw ? (cross <= kEpsilon) : (cross >= -kEpsilon)) {
                    continue;
                }

                bool containsPoint = false;
                for (size_t j = 0; j < m; ++j) {
                    const size_t idx = indices[j];
                    if (idx == prev || idx == curr || idx == next) {
                        continue;
                    }

                    const XZPoint p{vertices[idx].fX, vertices[idx].fZ};
                    if (PointInTriangleXZ(p, a, b, c)) {
                        containsPoint = true;
                        break;
                    }
                }

                if (containsPoint) {
                    continue;
                }

                triangles.push_back({prev, curr, next});
                indices.erase(indices.begin() + static_cast<std::ptrdiff_t>(i));
                earClipped = true;
                break;
            }

            if (!earClipped) {
                triangles.clear();
                return false;
            }
            ++safety;
        }

        if (indices.size() == 3) {
            triangles.push_back({indices[0], indices[1], indices[2]});
            return true;
        }

        triangles.clear();
        return false;
    }

    bool TryInterpolateTriangleY_(const cS3DVector3& p,
                                  const cS3DVector3& a,
                                  const cS3DVector3& b,
                                  const cS3DVector3& c,
                                  float& outY) {
        const float det = (b.fZ - c.fZ) * (a.fX - c.fX) + (c.fX - b.fX) * (a.fZ - c.fZ);
        if (std::abs(det) <= kEpsilon) {
            return false;
        }

        const float w0 = ((b.fZ - c.fZ) * (p.fX - c.fX) + (c.fX - b.fX) * (p.fZ - c.fZ)) / det;
        const float w1 = ((c.fZ - a.fZ) * (p.fX - c.fX) + (a.fX - c.fX) * (p.fZ - c.fZ)) / det;
        const float w2 = 1.0f - w0 - w1;

        if (w0 < -kEpsilon || w1 < -kEpsilon || w2 < -kEpsilon) {
            return false;
        }

        outY = w0 * a.fY + w1 * b.fY + w2 * c.fY;
        return true;
    }

    bool TryResolveInterpolatedY_(const float px,
                                  const float pz,
                                  const std::vector<cS3DVector3>& polygonVertices,
                                  const std::vector<TriangleIndices>& triangles,
                                  float& outY) {
        const cS3DVector3 samplePoint(px, 0.0f, pz);
        for (const auto& tri : triangles) {
            if (TryInterpolateTriangleY_(samplePoint,
                                         polygonVertices[tri.a],
                                         polygonVertices[tri.b],
                                         polygonVertices[tri.c],
                                         outY)) {
                return true;
            }
        }
        return false;
    }
}

std::vector<PlannedPaint> PolygonPlacer::ComputePlacements(
    const std::vector<cS3DVector3>& polygonVertices,
    const float densityPer100Sqm,
    const float densityVariation,
    const int32_t baseRotation,
    const bool randomRotation,
    cISTETerrain* terrain,
    const uint32_t seed,
    const PolygonHeightSamplingMode heightSamplingMode,
    WeightedPicker* picker,
    const uint32_t singlePropID,
    const size_t maxPlacements) {
    std::vector<PlannedPaint> result;
    if (polygonVertices.size() < 3 || densityPer100Sqm <= kEpsilon || maxPlacements == 0) {
        return result;
    }

    float minX = polygonVertices.front().fX;
    float maxX = minX;
    float minZ = polygonVertices.front().fZ;
    float maxZ = minZ;
    for (const auto& v : polygonVertices) {
        minX = std::min(minX, v.fX);
        maxX = std::max(maxX, v.fX);
        minZ = std::min(minZ, v.fZ);
        maxZ = std::max(maxZ, v.fZ);
    }

    const float cellSize = std::sqrt(100.0f / densityPer100Sqm);
    if (cellSize <= kEpsilon) {
        return result;
    }

    std::mt19937 rng(seed);
    const float clampedVariation = std::clamp(densityVariation, 0.0f, 1.0f);
    const float patchCellSize = cellSize * (3.0f + 5.0f * clampedVariation);
    const float jitterExtent = 0.4f + 0.2f * clampedVariation;
    std::uniform_real_distribution<float> jitterDist(-jitterExtent, jitterExtent);
    std::uniform_int_distribution<int32_t> rotDist(0, 3);

    std::vector<TriangleIndices> triangles;
    if (heightSamplingMode == PolygonHeightSamplingMode::InterpolateVertices &&
        !TriangulatePolygon_(polygonVertices, triangles)) {
        return result;
    }

    for (float x = minX; x <= maxX; x += cellSize) {
        for (float z = minZ; z <= maxZ; z += cellSize) {
            size_t placementAttempts = 1;
            if (clampedVariation > kEpsilon) {
                const float patchNoise = PatchNoise_(x + 0.5f * cellSize, z + 0.5f * cellSize, patchCellSize, seed);
                const float localDensityMultiplier = std::clamp(
                    1.0f + ((patchNoise - 0.5f) * 1.6f * clampedVariation),
                    0.2f,
                    1.8f);
                placementAttempts = static_cast<size_t>(localDensityMultiplier);
                const float fractionalAttempt = localDensityMultiplier - static_cast<float>(placementAttempts);
                std::bernoulli_distribution extraAttemptDist(fractionalAttempt);
                if (extraAttemptDist(rng)) {
                    ++placementAttempts;
                }
            }

            for (size_t attempt = 0; attempt < placementAttempts; ++attempt) {
                const float px = x + jitterDist(rng) * cellSize;
                const float pz = z + jitterDist(rng) * cellSize;
                if (!PointInPolygon_(px, pz, polygonVertices)) {
                    continue;
                }

                float py = 0.0f;
                if (heightSamplingMode == PolygonHeightSamplingMode::InterpolateVertices) {
                    if (!TryResolveInterpolatedY_(px, pz, polygonVertices, triangles, py)) {
                        continue;
                    }
                }
                else if (terrain) {
                    py = terrain->GetAltitude(px, pz);
                }
                else {
                    py = polygonVertices.front().fY;
                }

                const int32_t rotation = randomRotation ? rotDist(rng) : (baseRotation & 3);
                result.push_back({
                    cS3DVector3(px, py, pz),
                    rotation,
                    picker ? picker->Pick() : singlePropID
                });

                if (result.size() >= maxPlacements) {
                    return result;
                }
            }
        }
    }

    return result;
}

bool PolygonPlacer::CanTriangulate(const std::vector<cS3DVector3>& polygonVertices) {
    std::vector<TriangleIndices> triangles;
    return TriangulatePolygon_(polygonVertices, triangles);
}

bool PolygonPlacer::PointInPolygon_(const float px, const float pz, const std::vector<cS3DVector3>& polygon) {
    if (polygon.size() < 3) {
        return false;
    }

    bool inside = false;
    const size_t count = polygon.size();
    for (size_t i = 0, j = count - 1; i < count; j = i++) {
        const float zi = polygon[i].fZ;
        const float zj = polygon[j].fZ;
        const float xi = polygon[i].fX;
        const float xj = polygon[j].fX;

        const bool crosses = (zi > pz) != (zj > pz);
        if (!crosses) {
            continue;
        }

        const float denom = zj - zi;
        if (std::abs(denom) <= kEpsilon) {
            continue;
        }

        const float intersectX = (xj - xi) * (pz - zi) / denom + xi;
        if (px < intersectX) {
            inside = !inside;
        }
    }

    return inside;
}
