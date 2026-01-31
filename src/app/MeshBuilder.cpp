#include "MeshBuilder.hpp"

#include "raymath.h"
#include "spdlog/spdlog.h"

namespace thumb {
    Vector3 MeshBuilder::calculateModelCenter(const S3D::Record& record) {
        if (record.vertexBuffers.empty()) {
            return Vector3Zero();
        }

        const Vector3 min{
            record.bbMin.x,
            record.bbMin.y,
            record.bbMin.z,
        };
        const Vector3 max{
            record.bbMax.x,
            record.bbMax.y,
            record.bbMax.z,
        };
        return Vector3Scale(Vector3Add(min, max), 0.5f);
    }

    std::vector<MeshSource> MeshBuilder::collectMeshSources(const S3D::Record& record) {
        std::vector<MeshSource> sources;
        sources.reserve(record.animation.animatedMeshes.size());

        for (const auto& mesh : record.animation.animatedMeshes) {
            if (mesh.frames.empty()) {
                continue;
            }

            const auto& frame = mesh.frames.front();
            if (frame.vertBlock >= record.vertexBuffers.size() ||
                frame.indexBlock >= record.indexBuffers.size() ||
                frame.primBlock >= record.primitiveBlocks.size()) {
                continue;
            }

            MeshSource source;
            source.vertexBuffer = &record.vertexBuffers[frame.vertBlock];
            source.indexBuffer = &record.indexBuffers[frame.indexBlock];
            source.primitiveBlock = &record.primitiveBlocks[frame.primBlock];
            if (frame.matsBlock < record.materials.size()) {
                source.material = &record.materials[frame.matsBlock];
            }
            sources.push_back(source);
        }

        if (sources.empty() &&
            !record.vertexBuffers.empty() &&
            !record.indexBuffers.empty() &&
            !record.primitiveBlocks.empty()) {
            MeshSource fallback;
            fallback.vertexBuffer = &record.vertexBuffers.front();
            fallback.indexBuffer = &record.indexBuffers.front();
            fallback.primitiveBlock = &record.primitiveBlocks.front();
            if (!record.materials.empty()) {
                fallback.material = &record.materials.front();
            }
            sources.push_back(fallback);
        }

        return sources;
    }

    std::vector<uint16_t> MeshBuilder::expandPrimitives(const S3D::PrimitiveBlock& primitives,
                                                        std::span<const uint16_t> source) {
        std::vector<uint16_t> expanded;
        for (const auto& prim : primitives) {
            const size_t offset = prim.first;
            const size_t count = prim.length;
            if (offset >= source.size() || count == 0 || offset + count > source.size()) {
                continue;
            }

            switch (prim.type) {
            case 0: {
                for (size_t i = 0; i + 2 < count; i += 3) {
                    expanded.push_back(source[offset + i + 0]);
                    expanded.push_back(source[offset + i + 1]);
                    expanded.push_back(source[offset + i + 2]);
                }
                break;
            }
            case 1: {
                for (size_t i = 0; i + 2 < count; ++i) {
                    const uint16_t a = source[offset + i + 0];
                    const uint16_t b = source[offset + i + 1];
                    const uint16_t c = source[offset + i + 2];
                    if (i % 2 == 0) {
                        expanded.insert(expanded.end(), {a, b, c});
                    }
                    else {
                        expanded.insert(expanded.end(), {a, c, b});
                    }
                }
                break;
            }
            case 2: {
                // Maxis S3D often encodes quads; convert each group of 4 indices into 2 triangles
                if (count % 4 != 0) {
                    spdlog::debug("PRIM type 2 with non-multiple-of-4 count {} at offset {} — skipping", count,
                                  offset);
                    break;
                }
                for (size_t i = 0; i + 3 < count; i += 4) {
                    const uint16_t a = source[offset + i + 0];
                    const uint16_t b = source[offset + i + 1];
                    const uint16_t c = source[offset + i + 2];
                    const uint16_t d = source[offset + i + 3];
                    expanded.insert(expanded.end(), {a, b, c});
                    expanded.insert(expanded.end(), {a, c, d});
                }
                break;
            }
            case 3: {
                // Unknown/unsupported primitive type — log for investigation
                spdlog::debug("Encountered unsupported PRIM type 3 (first {}, length {})", prim.first, prim.length);
                break;
            }
            default:
                spdlog::debug("Encountered unsupported PRIM type {} (first {}, length {})", prim.type, prim.first,
                              prim.length);
                break;
            }
        }
        return expanded;
    }

    bool MeshBuilder::buildMeshFromSource(const MeshSource& source,
                                          const Vector3& center,
                                          const float yLift,
                                          Mesh& mesh,
                                          const bool preserveOriginalSpace) {
        if (!source.vertexBuffer || !source.indexBuffer || !source.primitiveBlock) {
            return false;
        }

        const auto expandedIndices = expandPrimitives(*source.primitiveBlock,
                                                      source.indexBuffer->indices);
        if (source.vertexBuffer->vertices.empty() || expandedIndices.size() < 3) {
            return false;
        }

        mesh = {};
        mesh.vertexCount = static_cast<int>(source.vertexBuffer->vertices.size());
        mesh.triangleCount = static_cast<int>(expandedIndices.size() / 3);
        mesh.vertices = static_cast<float*>(MemAlloc(mesh.vertexCount * 3 * sizeof(float)));
        mesh.normals = static_cast<float*>(MemAlloc(mesh.vertexCount * 3 * sizeof(float)));
        mesh.texcoords = static_cast<float*>(MemAlloc(mesh.vertexCount * 2 * sizeof(float)));
        mesh.colors = static_cast<unsigned char*>(MemAlloc(mesh.vertexCount * 4));
        mesh.indices = static_cast<unsigned short*>(
            MemAlloc(expandedIndices.size() * sizeof(unsigned short)));

        if (!mesh.vertices || !mesh.normals || !mesh.texcoords || !mesh.colors || !mesh.indices) {
            UnloadMesh(mesh);
            return false;
        }

        const float yOffset = preserveOriginalSpace ? 0.0f : yLift;

        for (int i = 0; i < mesh.vertexCount; ++i) {
            const auto& vert = source.vertexBuffer->vertices[i];
            if (preserveOriginalSpace) {
                mesh.vertices[i * 3 + 0] = vert.position.x;
                mesh.vertices[i * 3 + 1] = vert.position.y;
                mesh.vertices[i * 3 + 2] = vert.position.z;
            }
            else {
                mesh.vertices[i * 3 + 0] = vert.position.x - center.x;
                mesh.vertices[i * 3 + 1] = vert.position.y - center.y + yOffset;
                mesh.vertices[i * 3 + 2] = vert.position.z - center.z;
            }
            mesh.texcoords[i * 2 + 0] = vert.uv.x;
            mesh.texcoords[i * 2 + 1] = vert.uv.y;
            mesh.colors[i * 4 + 0] = static_cast<unsigned char>(vert.color.x * 255.0f);
            mesh.colors[i * 4 + 1] = static_cast<unsigned char>(vert.color.y * 255.0f);
            mesh.colors[i * 4 + 2] = static_cast<unsigned char>(vert.color.z * 255.0f);
            mesh.colors[i * 4 + 3] = static_cast<unsigned char>(vert.color.w * 255.0f);
        }

        std::memcpy(mesh.indices, expandedIndices.data(),
                    expandedIndices.size() * sizeof(unsigned short));

        std::vector<Vector3> normalAccum(mesh.vertexCount, Vector3Zero());
        for (size_t i = 0; i + 2 < expandedIndices.size(); i += 3) {
            const uint16_t i0 = expandedIndices[i + 0];
            const uint16_t i1 = expandedIndices[i + 1];
            const uint16_t i2 = expandedIndices[i + 2];
            if (i0 >= mesh.vertexCount || i1 >= mesh.vertexCount || i2 >= mesh.vertexCount) {
                continue;
            }

            const Vector3 v0{
                mesh.vertices[i0 * 3 + 0],
                mesh.vertices[i0 * 3 + 1],
                mesh.vertices[i0 * 3 + 2],
            };
            const Vector3 v1{
                mesh.vertices[i1 * 3 + 0],
                mesh.vertices[i1 * 3 + 1],
                mesh.vertices[i1 * 3 + 2],
            };
            const Vector3 v2{
                mesh.vertices[i2 * 3 + 0],
                mesh.vertices[i2 * 3 + 1],
                mesh.vertices[i2 * 3 + 2],
            };

            const Vector3 edge1 = Vector3Subtract(v1, v0);
            const Vector3 edge2 = Vector3Subtract(v2, v0);
            Vector3 normal = Vector3Normalize(Vector3CrossProduct(edge1, edge2));
            if (Vector3Length(normal) == 0.0f) {
                continue;
            }
            normalAccum[i0] = Vector3Add(normalAccum[i0], normal);
            normalAccum[i1] = Vector3Add(normalAccum[i1], normal);
            normalAccum[i2] = Vector3Add(normalAccum[i2], normal);
        }

        for (int i = 0; i < mesh.vertexCount; ++i) {
            Vector3 normal = Vector3Normalize(normalAccum[i]);
            mesh.normals[i * 3 + 0] = normal.x;
            mesh.normals[i * 3 + 1] = normal.y;
            mesh.normals[i * 3 + 2] = normal.z;
        }

        UploadMesh(&mesh, false);
        return true;
    }
} // namespace thumb
