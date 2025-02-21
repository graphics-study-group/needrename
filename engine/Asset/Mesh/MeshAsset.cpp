#include "MeshAsset.h"
#include <fstream>
#include <cassert>
#include <map>
#include <Reflection/serialization.h>

namespace Engine
{
    MeshAsset::MeshAsset()
    {
    }

    MeshAsset::~MeshAsset()
    {
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

        // save base class (such as GUID)
        Asset::save_asset_to_archive(archive);
    }

    void MeshAsset::load_asset_from_archive(Serialization::Archive &archive)
    {
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

        Asset::load_asset_from_archive(archive);
    }
}
