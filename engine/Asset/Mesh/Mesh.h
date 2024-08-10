#ifndef RESOURCE_MESH_MESH_INCLUDED
#define RESOURCE_MESH_MESH_INCLUDED

#include <vector>
#include <filesystem>
#include <cereal/cereal.hpp>
#include <cereal/types/vector.hpp>
#include "tiny_obj_loader.h"
#include "Asset/Asset.h"

namespace Engine
{
    class Mesh: public Asset
    {
    public:
        Mesh(std::weak_ptr <AssetManager> manager);
        virtual ~Mesh();

        /// @brief Load mesh data. (no material)
        virtual void Load() override;
        virtual void Unload() override;

        void LoadFromTinyobj(const tinyobj::attrib_t & attrib, const std::vector<tinyobj::shape_t> & shapes);

        size_t GetSubmeshCount() const;
        inline const std::vector <size_t> & GetOffsets() const { return m_offsets; }
        inline const std::vector <size_t> & GetTriangle_vert_ids() const { return m_triangle_vert_ids; }
        inline const std::vector <size_t> & GetTriangle_normal_ids() const { return m_triangle_normal_ids; }
        inline const std::vector <size_t> & GetTriangle_uv_ids() const { return m_triangle_uv_ids; }
        inline const std::vector <float> & GetPositions() const { return m_positions; }
        inline const std::vector <float> & GetUVs() const { return m_uvs; }
    
    protected:
        std::vector <size_t> m_offsets;
        std::vector <size_t> m_triangle_vert_ids;
        std::vector <size_t> m_triangle_normal_ids;
        std::vector <size_t> m_triangle_uv_ids;
        std::vector <float> m_positions;
        std::vector <float> m_uvs;
    
    private:
        friend class cereal::access;
    
        template <class Archive>
        void serialize(Archive & ar)
        {
            ar(m_offsets, m_triangle_vert_ids, m_triangle_normal_ids, m_triangle_uv_ids, m_positions, m_uvs);
        }
    };
}

#endif // RESOURCE_MESH_MESH_INCLUDED