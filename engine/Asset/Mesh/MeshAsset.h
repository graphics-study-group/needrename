#ifndef ASSET_MESH_MESHASSET_INCLUDED
#define ASSET_MESH_MESHASSET_INCLUDED

#include <tiny_obj_loader.h>
#include <Asset/Asset.h>
#include <Render/Renderer/VertexStruct.h>
#include <meta_engine/reflection.hpp>
#include <vector>

namespace Engine
{
    class REFL_SER_CLASS(REFL_WHITELIST) MeshAsset: public Asset
    {
        REFL_SER_BODY()
    public:
        REFL_ENABLE MeshAsset();
        virtual ~MeshAsset();

        void LoadFromTinyobj(const tinyobj::attrib_t & attrib, const std::vector<tinyobj::shape_t> & shapes);

        struct Submesh
        {
            std::vector <uint32_t> m_indices {};
            std::vector <VertexStruct::VertexPosition> m_positions {};
            std::vector <VertexStruct::VertexAttribute> m_attributes {};
        };

        REFL_ENABLE size_t GetSubmeshCount() const;
        REFL_ENABLE uint32_t GetSubmeshVertexIndexCount(size_t submesh_idx) const;
        REFL_ENABLE uint32_t GetSubmeshVertexCount(size_t submesh_idx) const;
        REFL_ENABLE uint64_t GetSubmeshExpectedBufferSize(size_t submesh_idx) const;

        virtual void save_asset_to_archive(Serialization::Archive& archive) const override;
        virtual void load_asset_from_archive(Serialization::Archive& archive) override;
    
        std::string m_name;
        std::vector<Submesh> m_submeshes {};
    };
}

#endif // ASSET_MESH_MESHASSET_INCLUDED
