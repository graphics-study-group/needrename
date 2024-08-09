#include "Mesh.h"
#include <cereal/cereal.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/archives/binary.hpp>
#include <tiny_obj_loader.h>
#include <fstream>
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
        std::ifstream is(mesh_path, std::ios::binary);
        if (is.is_open())
        {
            cereal::BinaryInputArchive archive(is);
            archive(*this);
        }
        else
        {
            throw std::runtime_error("Failed to open file: " + mesh_path.string());
        }
    }

    void Mesh::Unload()
    {
        throw std::runtime_error("Not implemented");
    }

    void Mesh::LoadFromTinyobj(const tinyobj::attrib_t &attrib, const std::vector<tinyobj::shape_t> &shapes)
    {
        m_positions.clear();
        m_uvs.clear();
        m_offsets.clear();
        m_triangles.clear();

        m_positions = attrib.vertices;
        m_uvs = attrib.texcoords;

        for (const auto &shape : shapes)
        {
            m_offsets.push_back(m_triangles.size());
            for (const auto &index : shape.mesh.indices)
            {
                m_triangles.push_back(index.vertex_index);
            }
        }
    }

    size_t Mesh::GetSubmeshCount() const
    {
        return m_offsets.size();
    }
}