#ifndef ASSET_MESH_MESHASSET_INCLUDED
#define ASSET_MESH_MESHASSET_INCLUDED

#include <Asset/Asset.h>
#include <Reflection/macros.h>
#include <Render/Renderer/VertexStruct.h>
#include <vector>

namespace Engine {
    class ObjLoader;

    class REFL_SER_CLASS(REFL_WHITELIST) MeshAsset : public Asset {
        REFL_SER_BODY(MeshAsset)
    public:
        REFL_ENABLE MeshAsset();
        virtual ~MeshAsset();

        friend class ObjLoader;

        struct Submesh {
            std::vector<uint32_t> m_indices{};
            std::vector<VertexStruct::VertexPosition> m_positions{};
            std::vector<VertexStruct::VertexAttributeBasic> m_attributes_basic{};
            std::vector<VertexStruct::VertexAttributeExtended> m_attributes_extended{};
            std::vector<VertexStruct::VertexAttributeSkinned> m_attributes_skinned{};
        };

        REFL_ENABLE size_t GetSubmeshCount() const;
        REFL_ENABLE uint32_t GetSubmeshVertexIndexCount(size_t submesh_idx) const;
        REFL_ENABLE uint32_t GetSubmeshVertexCount(size_t submesh_idx) const;

        [[deprecated("Expected buffer size is calculated by HomogeneousMesh instead.")]]
        REFL_ENABLE uint64_t GetSubmeshExpectedBufferSize(size_t submesh_idx) const;

        virtual void save_asset_to_archive(Serialization::Archive &archive) const override;
        virtual void load_asset_from_archive(Serialization::Archive &archive) override;

        std::string m_name{};
        std::vector<Submesh> m_submeshes{};
    };
} // namespace Engine

#endif // ASSET_MESH_MESHASSET_INCLUDED
