#ifndef RESOURCE_MESH_MESH_INCLUDED
#define RESOURCE_MESH_MESH_INCLUDED

#include <vector>
#include <filesystem>
#include <GLAD/glad.h>
#include <cereal/cereal.hpp>
#include <cereal/types/vector.hpp>
#include "tiny_obj_loader.h"
#include "Asset/Asset.h"

namespace Engine
{
    class Mesh: public Asset
    {
    public:
        Mesh();
        virtual ~Mesh();

        /// @brief Load mesh data. (no material)
        virtual void Load() override;
        virtual void Unload() override;

        void LoadFromTinyobj(const tinyobj::attrib_t & attrib, const std::vector<tinyobj::shape_t> & shapes);
    
    protected:
        std::vector <size_t> m_offsets;
        std::vector <size_t> m_triangles;
        std::vector <float> m_position;
        std::vector <float> m_uv;
    
    private:
        friend class cereal::access;
    
        template <class Archive>
        inline void serialize(Archive & ar) const
        {
            ar(m_offsets, m_triangles, m_position, m_uv);
        }
    };
}

#endif // RESOURCE_MESH_MESH_INCLUDED