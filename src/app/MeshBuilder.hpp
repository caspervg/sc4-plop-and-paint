#pragma once

#include <span>
#include <vector>

#include "raylib.h"
#include "S3DReader.h"

namespace thumb {
    struct MeshSource {
        const S3D::VertexBuffer* vertexBuffer = nullptr;
        const S3D::IndexBuffer* indexBuffer = nullptr;
        const S3D::PrimitiveBlock* primitiveBlock = nullptr;
        const S3D::Material* material = nullptr;
    };

    class MeshBuilder {
    public:
        static Vector3 calculateModelCenter(const S3D::Record& record);
        static std::vector<MeshSource> collectMeshSources(const S3D::Record& record);
        static std::vector<uint16_t> expandPrimitives(const S3D::PrimitiveBlock& primitives,
                                                      std::span<const uint16_t> source);
        static bool buildMeshFromSource(const MeshSource& source,
                                        const Vector3& center,
                                        float yLift,
                                        Mesh& mesh,
                                        bool preserveOriginalSpace = false);
    };
} // namespace thumb
