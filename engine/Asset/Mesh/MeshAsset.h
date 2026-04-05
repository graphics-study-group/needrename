#ifndef ASSET_MESH_MESHASSET
#define ASSET_MESH_MESHASSET

#include "Render/Renderer/VertexAttribute.h"

#include <Asset/Asset.h>
#include <Reflection/macros.h>
#include <variant>
#include <vector>

namespace Engine {
    class ObjLoader;
    class VertexAttribute;

    /**
     * @brief An asset containing a mesh to be rendered.
     * 
     * It can have multiple submeshes. Each submesh has a dedicated binary
     * buffer that contains all vertex information.
     */
    class REFL_SER_CLASS(REFL_WHITELIST) MeshAsset : public Asset {
        REFL_SER_BODY(MeshAsset)

    public:
        REFL_ENABLE MeshAsset();
        virtual ~MeshAsset();

        friend class ObjLoader;

        /**
         * @brief A submesh.
         * 
         * It contains an index buffer and a vertex buffer, along with
         * specifications of the vertex attributes.
         */
        struct Submesh {
            /// @brief Index buffer.
            /// @remark It should be guaranteed that its values are smaller than
            /// `vertex_count`. Otherwise illegal memory accesses might happen.
            std::vector<uint32_t> m_indices{};

            /// @brief Raw buffer containing all vertex attribute data
            std::vector<std::byte> m_vertex_attributes{};

            /// @brief Vertex attribute specification. 
            struct Attributes {
                /// @brief Type of the vertex attribute
                VertexAttributeType type{VertexAttributeType::Unused};
                /// @brief Offset of this attribute into the buffer in bytes.
                size_t buffer_offset{0};
                /// @brief Size of this attribute in bytes.
                size_t buffer_size{0};
            };

            /// @brief How many vertices are in this submesh.
            /// It does not reflect actual drawing process as vertices are
            /// first indexed by the index buffer.
            uint32_t vertex_count{};
            Attributes positions{};
            Attributes color{}, normal{}, texcoord0{};
            Attributes tangent{}, texcoord1{}, texcoord2{}, texcoord3{};
            Attributes bone_indices{}, bone_weights{};

            /**
             * @brief Get a `VertexAttribute` descibing this submesh.
             */
            VertexAttribute ToVertexAttributeFormat() const noexcept;

            /**
             * @brief Write out all vertex attributes to the given buffer.
             *
             * The buffer is assumed to be large enough.
             */
            void WriteVertexAttributeBuffer(std::byte *buf) const noexcept;

            /**
             * @brief Write out all indices to the given buffer.
             *
             * The buffer is assumed to be large enough.
             */
            void WriteIndexBuffer(std::byte *buf) const noexcept;

            /**
             * @brief Get the total buffer size of this submesh.
             *
             * @return `m_indices.size() * sizeof(uint32_t) + m_vertex_attributes.size()`
             */
            size_t GetTotalBufferSize() const noexcept {
                return m_indices.size() * sizeof(uint32_t) + m_vertex_attributes.size();
            }
        };

        /**
         * @brief Get the number of submeshes.
         */
        REFL_ENABLE size_t GetSubmeshCount() const;
        
        /**
         * @brief Get the index count of a submesh.
         */
        REFL_ENABLE uint32_t GetSubmeshVertexIndexCount(size_t submesh_idx) const;

        /**
         * @brief Get the vertex count of a submesh.
         */
        REFL_ENABLE uint32_t GetSubmeshVertexCount(size_t submesh_idx) const;

        virtual void save_asset_to_archive(Serialization::Archive &archive) const override;
        virtual void load_asset_from_archive(Serialization::Archive &archive) override;

        std::string m_name{};
        std::vector<Submesh> m_submeshes{};
    };
} // namespace Engine

#endif // ASSET_MESH_MESHASSET
