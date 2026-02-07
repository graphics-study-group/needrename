#ifndef ASSET_MESH_MESHASSET
#define ASSET_MESH_MESHASSET

#include "Render/Renderer/VertexAttribute.h"

#include <Asset/Asset.h>
#include <Reflection/macros.h>
#include <vector>
#include <variant>

namespace Engine {
    class ObjLoader;
    class VertexAttribute;

    class REFL_SER_CLASS(REFL_WHITELIST) MeshAsset : public Asset {
        REFL_SER_BODY(MeshAsset)

    public:
        REFL_ENABLE MeshAsset();
        virtual ~MeshAsset();

        friend class ObjLoader;

        struct Submesh {
            std::vector<uint32_t> m_indices{};

            // Raw buffer containing all vertex attribute data
            std::vector <std::byte> m_vertex_attributes;

            struct Attributes {
                VertexAttributeType type{VertexAttributeType::Unused};
                // Offset of this attribute into the buffer in bytes.
                size_t buffer_offset{0};
                // Size of this attribute in bytes.
                size_t buffer_size{0};
            };

            uint32_t vertex_count {};
            Attributes positions {};
            Attributes color {}, normal {}, texcoord0 {};
            Attributes tangent {}, texcoord1 {}, texcoord2 {}, texcoord3 {};
            Attributes bone_indices {}, bone_weights {};

            /**
             * @brief Get a `VertexAttribute` descibing this submesh.
             */
            VertexAttribute ToVertexAttributeFormat() const noexcept;

            /**
             * @brief Write out all vertex attributes to the given buffer.
             * 
             * The buffer is assumed to be large enough.
             */
            void WriteVertexAttributeBuffer(std::byte * buf) const noexcept;

            /**
             * @brief Write out all indices to the given buffer.
             * 
             * The buffer is assumed to be large enough.
             */
            void WriteIndexBuffer(std::byte * buf) const noexcept;

            /**
             * @brief Get the total buffer size of this submesh.
             * 
             * @return `m_indices.size() * sizeof(uint32_t) + m_vertex_attributes.size()`
             */
            size_t GetTotalBufferSize() const noexcept {
                return m_indices.size() * sizeof(uint32_t) + m_vertex_attributes.size();
            }
        };

        REFL_ENABLE size_t GetSubmeshCount() const;
        REFL_ENABLE uint32_t GetSubmeshVertexIndexCount(size_t submesh_idx) const;
        REFL_ENABLE uint32_t GetSubmeshVertexCount(size_t submesh_idx) const;

        virtual void save_asset_to_archive(Serialization::Archive &archive) const override;
        virtual void load_asset_from_archive(Serialization::Archive &archive) override;

        std::string m_name{};
        std::vector<Submesh> m_submeshes{};
    };
} // namespace Engine

#endif // ASSET_MESH_MESHASSET
