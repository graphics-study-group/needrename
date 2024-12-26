#include "MeshAsset.h"
#include <fstream>
#include <cassert>
#include <map>

namespace Engine
{
    MeshAsset::MeshAsset()
    {
    }

    MeshAsset::~MeshAsset()
    {
    }

    void MeshAsset::LoadFromTinyobj(const tinyobj::attrib_t &attrib, const std::vector<tinyobj::shape_t> &shapes)
    {
        m_submeshes.clear();

        const auto &positions = attrib.vertices;
        const auto &normals = attrib.normals;
        const auto &uvs = attrib.texcoords;
        const auto &colors = attrib.colors;

        for (const auto &shape : shapes)
        {
            m_submeshes.emplace_back();
            auto &submesh = m_submeshes.back();
            uint32_t vertex_id = 0;
            std::map<std::tuple<int, int, int>, uint32_t> vertex_id_map;
            for (const auto &index : shape.mesh.indices)
            {
                std::tuple<int, int, int> key(index.vertex_index, index.normal_index, index.texcoord_index);
                if (vertex_id_map.find(key) == vertex_id_map.end())
                {
                    vertex_id_map[key] = vertex_id++;
                    submesh.m_positions.push_back(VertexStruct::VertexPosition{
                        .position = {positions[index.vertex_index * 3], positions[index.vertex_index * 3 + 1], positions[index.vertex_index * 3 + 2]}});
                    VertexStruct::VertexAttribute attr = {};
                    if (colors.size() > 0)
                    {
                        attr.color[0] = colors[index.vertex_index * 3];
                        attr.color[1] = colors[index.vertex_index * 3 + 1];
                        attr.color[2] = colors[index.vertex_index * 3 + 2];
                    }
                    if (index.normal_index >= 0)
                    {
                        attr.normal[0] = normals[index.normal_index * 3];
                        attr.normal[1] = normals[index.normal_index * 3 + 1];
                        attr.normal[2] = normals[index.normal_index * 3 + 2];
                    }
                    if (index.texcoord_index >= 0)
                    {
                        attr.texcoord1[0] = uvs[index.texcoord_index * 2];
                        attr.texcoord1[1] = uvs[index.texcoord_index * 2 + 1];
                    }
                    submesh.m_attributes.push_back(attr);
                }
                submesh.m_indices.push_back(vertex_id_map[key]);
            }
        }

        Asset::SetValid(true);
    }

    size_t MeshAsset::GetSubmeshCount() const
    {
        return m_submeshes.size();
    }

    uint32_t MeshAsset::GetSubmeshVertexIndexCount(size_t submesh_idx) const
    {
        return m_submeshes[submesh_idx].m_indices.size();
    }
    uint32_t MeshAsset::GetSubmeshVertexCount(size_t submesh_idx) const
    {
        return m_submeshes[submesh_idx].m_positions.size();
    }
    uint64_t MeshAsset::GetSubmeshExpectedBufferSize(size_t submesh_idx) const
    {
        return GetSubmeshVertexIndexCount(submesh_idx) * sizeof(uint32_t) + GetSubmeshVertexCount(submesh_idx) * VertexStruct::VERTEX_TOTAL_SIZE;
    }

    void MeshAsset::save_asset_to_archive(Serialization::Archive &archive) const
    {
        // save base class (such as GUID)
        Asset::save_asset_to_archive(archive);

        auto &data = archive.m_context->extra_data;
        assert(data.empty());

        size_t reserved_size = sizeof(size_t); // submesh count
        size_t submesh_count = GetSubmeshCount();
        for(size_t i = 0; i < submesh_count; i++)
        {
            reserved_size += sizeof(size_t) * 2 + GetSubmeshExpectedBufferSize(i); // size of indices + size of vertex + all indices and vertex data
        }
        data.reserve(reserved_size);

        data.insert(
            data.end(),
            reinterpret_cast<const std::byte *>(&submesh_count),
            reinterpret_cast<const std::byte *>((&submesh_count) + 1));
        for(size_t i = 0; i < submesh_count; i++)
        {
            size_t m_indices_size = m_submeshes[i].m_indices.size();
            data.insert(
                data.end(),
                reinterpret_cast<const std::byte *>(&m_indices_size),
                reinterpret_cast<const std::byte *>((&m_indices_size) + 1));
            data.insert(
                data.end(),
                reinterpret_cast<const std::byte *>(m_submeshes[i].m_indices.data()),
                reinterpret_cast<const std::byte *>(m_submeshes[i].m_indices.data() + m_submeshes[i].m_indices.size()));

            size_t m_vertex_size = m_submeshes[i].m_positions.size();
            data.insert(
                data.end(),
                reinterpret_cast<const std::byte *>(&m_vertex_size),
                reinterpret_cast<const std::byte *>((&m_vertex_size) + 1));
            data.insert(
                data.end(),
                reinterpret_cast<const std::byte *>(m_submeshes[i].m_positions.data()),
                reinterpret_cast<const std::byte *>(m_submeshes[i].m_positions.data() + m_submeshes[i].m_positions.size()));
            data.insert(
                data.end(),
                reinterpret_cast<const std::byte *>(m_submeshes[i].m_attributes.data()),
                reinterpret_cast<const std::byte *>(m_submeshes[i].m_attributes.data() + m_submeshes[i].m_attributes.size()));
        }
    }

    void MeshAsset::load_asset_from_archive(Serialization::Archive &archive)
    {
        Asset::load_asset_from_archive(archive);

        auto &data = archive.m_context->extra_data;
        size_t offset = 0;

        size_t submesh_count = *reinterpret_cast<const size_t *>(&data[offset]);
        offset += sizeof(size_t);
        m_submeshes.resize(submesh_count);
        for(size_t i = 0; i < submesh_count; i++)
        {
            size_t m_indices_size = *reinterpret_cast<const size_t *>(&data[offset]);
            offset += sizeof(size_t);
            m_submeshes[i].m_indices.resize(m_indices_size);
            std::memcpy(m_submeshes[i].m_indices.data(), &data[offset], m_indices_size * sizeof(decltype(m_submeshes[i].m_indices)::value_type));
            offset += m_indices_size * sizeof(decltype(m_submeshes[i].m_indices)::value_type);

            size_t m_vertex_size = *reinterpret_cast<const size_t *>(&data[offset]);
            offset += sizeof(size_t);
            m_submeshes[i].m_positions.resize(m_vertex_size);
            std::memcpy(m_submeshes[i].m_positions.data(), &data[offset], m_vertex_size * sizeof(decltype(m_submeshes[i].m_positions)::value_type));
            offset += m_vertex_size * sizeof(decltype(m_submeshes[i].m_positions)::value_type);
            m_submeshes[i].m_attributes.resize(m_vertex_size);
            std::memcpy(m_submeshes[i].m_attributes.data(), &data[offset], m_vertex_size * sizeof(decltype(m_submeshes[i].m_attributes)::value_type));
            offset += m_vertex_size * sizeof(decltype(m_submeshes[i].m_attributes)::value_type);
        }
    }
}
