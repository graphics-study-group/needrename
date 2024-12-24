#ifndef ASSET_MESH_MESHASSET_INCLUDED
#define ASSET_MESH_MESHASSET_INCLUDED

#include <vector>
#include <filesystem>
#include <tiny_obj_loader.h>
#include <Asset/Asset.h>
#include <meta_engine/reflection.hpp>

namespace Engine
{
    class REFL_SER_CLASS(REFL_WHITELIST) MeshAsset: public Asset
    {
        REFL_SER_BODY()
    public:
        REFL_ENABLE MeshAsset();
        virtual ~MeshAsset();

        void LoadFromTinyobj(const tinyobj::attrib_t & attrib, const std::vector<tinyobj::shape_t> & shapes);

        REFL_ENABLE size_t GetSubmeshCount() const;
        REFL_ENABLE const std::vector <size_t> & GetOffsets() const;
        REFL_ENABLE const std::vector <size_t> & GetTriangle_vert_ids() const;
        REFL_ENABLE const std::vector <size_t> & GetTriangle_normal_ids() const;
        REFL_ENABLE const std::vector <size_t> & GetTriangle_uv_ids() const;
        REFL_ENABLE const std::vector <float> & GetPositions() const;
        REFL_ENABLE const std::vector <float> & GetNormals() const;
        REFL_ENABLE const std::vector <float> & GetUVs() const;

        virtual void save_asset_to_archive(Serialization::Archive& archive) const override;
        virtual void load_asset_from_archive(Serialization::Archive& archive) override;
    
    protected:
        std::vector <size_t> m_offsets {};
        std::vector <size_t> m_triangle_vert_ids {};
        std::vector <size_t> m_triangle_normal_ids {};
        std::vector <size_t> m_triangle_uv_ids {};
        std::vector <float> m_positions {};
        std::vector <float> m_normals {};
        std::vector <float> m_uvs {}; 
    };
}

#endif // ASSET_MESH_MESHASSET_INCLUDED
