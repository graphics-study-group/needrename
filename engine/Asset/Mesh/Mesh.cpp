#include "Mesh.h"
#include <tiny_obj_loader.h>
#include <iostream>
#include <cassert>

namespace Engine
{
    Mesh::Mesh()
    {
    }

    Mesh::~Mesh()
    {
    }

    void Mesh::Load()
    {
        std::filesystem::path mesh_path = GetAssetPath();
    }

    void Mesh::Unload()
    {
        throw std::runtime_error("Not implemented");
    }

    void Mesh::LoadFromTinyobj(const tinyobj::attrib_t & attrib, const std::vector<tinyobj::shape_t> & shapes)
    {
        m_position.clear();
        m_uv.clear();
        m_offsets.clear();
        m_triangles.clear();

        m_position = attrib.vertices;
        m_uv = attrib.texcoords;

        for (const auto & shape : shapes)
        {
            m_offsets.push_back(m_triangles.size());
            for (const auto & index : shape.mesh.indices)
            {
                m_triangles.push_back(index.vertex_index);
            }
        }
    }
}