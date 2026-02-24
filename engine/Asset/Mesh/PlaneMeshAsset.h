#ifndef ASSET_MESH_PLANEMESHASSET
#define ASSET_MESH_PLANEMESHASSET

#include "Asset/Mesh/MeshAsset.h"
#include "Reflection/macros.h"
#include <cassert>
#include <cstring>

namespace Engine {
    /**
     * @brief A mesh asset containing a XY-plane facing Z-positive for testing.
     * 
     * This asset should not be serialized or deserialized.
     */
    class REFL_SER_CLASS(REFL_WHITELIST) PlaneMeshAsset : public MeshAsset {
        REFL_SER_BODY(PlaneMeshAsset)
        constexpr static std::array<float, 4*(3*3+2)> ATTRIBUTE_BUFFERS = {
            // Position
            1.0f, -1.0f, 0.0f,
            1.0f, 1.0f, 0.0f,
            -1.0f, 1.0f, 0.0f,
            -1.0f, -1.0f, 0.0f,
            // Color
            1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f,
            // Normal
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f,
            // Texcoord 0
            1.0f, 0.0f,
            1.0f, 1.0f,
            0.0f, 1.0f,
            0.0f, 0.0f
        };
    public:
        PlaneMeshAsset() : MeshAsset() {
            this->m_submeshes.resize(1);
            this->m_submeshes[0] = {
                .m_indices = {0, 3, 2, 0, 2, 1},
                .m_vertex_attributes = {},
                .vertex_count = 4,
                .positions = Submesh::Attributes{
                    .type = VertexAttributeType::SFloat32x3,
                    .buffer_offset = 0,
                    .buffer_size = sizeof(float) * 4 * 3
                },
                .color = Submesh::Attributes{
                    .type = VertexAttributeType::SFloat32x3,
                    .buffer_offset = sizeof(float) * 4 * 3,
                    .buffer_size = sizeof(float) * 4 * 3
                },
                .normal = Submesh::Attributes{
                    .type = VertexAttributeType::SFloat32x3,
                    .buffer_offset = sizeof(float) * 4 * 3 * 2,
                    .buffer_size = sizeof(float) * 4 * 3
                },
                .texcoord0 = Submesh::Attributes{
                    .type = VertexAttributeType::SFloat32x2,
                    .buffer_offset = sizeof(float) * 4 * 3 * 3,
                    .buffer_size = sizeof(float) * 4 * 2
                }
            };
            this->m_submeshes[0].m_vertex_attributes.resize(sizeof(float) * ATTRIBUTE_BUFFERS.size());
            std::memcpy(
                m_submeshes[0].m_vertex_attributes.data(),
                ATTRIBUTE_BUFFERS.data(),
                m_submeshes[0].m_vertex_attributes.size()
            );
        }

        virtual void save_asset_to_archive(Serialization::Archive &) const override {
            assert(!"Serializing a run-time only test asset.");
        }
        virtual void load_asset_from_archive(Serialization::Archive &) override {
            assert(!"De-serializing a run-time only test asset.");
        }
    };
}

#endif // ASSET_MESH_PLANEMESHASSET
