#include "MeshAsset.h"
#include <tiny_obj_loader.h>
#include <fstream>
#include <cassert>

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
        m_positions.clear();
        m_normals.clear();
        m_uvs.clear();
        m_offsets.clear();
        m_triangle_vert_ids.clear();
        m_triangle_normal_ids.clear();
        m_triangle_uv_ids.clear();

        m_positions = attrib.vertices;
        m_normals = attrib.normals;
        m_uvs = attrib.texcoords;

        for (const auto &shape : shapes)
        {
            m_offsets.push_back(m_triangle_vert_ids.size());
            for (const auto &index : shape.mesh.indices)
            {
                m_triangle_vert_ids.push_back(index.vertex_index);
                m_triangle_normal_ids.push_back(index.normal_index);
                m_triangle_uv_ids.push_back(index.texcoord_index);
            }
        }
    }

    size_t MeshAsset::GetSubmeshCount() const
    {
        return m_offsets.size();
    }

    const std::vector<size_t> &MeshAsset::GetOffsets() const
    {
        return m_offsets;
    }

    const std::vector<size_t> &MeshAsset::GetTriangle_vert_ids() const
    {
        return m_triangle_vert_ids;
    }

    const std::vector<size_t> &MeshAsset::GetTriangle_normal_ids() const
    {
        return m_triangle_normal_ids;
    }

    const std::vector<size_t> &MeshAsset::GetTriangle_uv_ids() const
    {
        return m_triangle_uv_ids;
    }

    const std::vector<float> &MeshAsset::GetPositions() const
    {
        return m_positions;
    }

    const std::vector<float> &MeshAsset::GetNormals() const
    {
        return m_normals;
    }

    const std::vector<float> &MeshAsset::GetUVs() const
    {
        return m_uvs;
    }

    void MeshAsset::save_asset_to_archive(Serialization::Archive &archive) const
    {
        Asset::save_asset_to_archive(archive);

        auto &data = archive.m_context->extra_data;
        assert(data.empty());
        size_t reserved_size = sizeof(size_t) * 7 + sizeof(size_t) * m_offsets.size() + sizeof(size_t) * m_triangle_vert_ids.size() + sizeof(size_t) * m_triangle_normal_ids.size() + sizeof(size_t) * m_triangle_uv_ids.size() + sizeof(float) * m_positions.size() + sizeof(float) * m_normals.size() + sizeof(float) * m_uvs.size();
        data.reserve(reserved_size);

        size_t m_offsets_size = m_offsets.size();
        data.insert(
            data.end(),
            reinterpret_cast<const std::byte *>(&m_offsets_size),
            reinterpret_cast<const std::byte *>(&m_offsets_size + 1));
        data.insert(
            data.end(),
            reinterpret_cast<const std::byte *>(m_offsets.data()),
            reinterpret_cast<const std::byte *>(m_offsets.data() + m_offsets.size()));
        
        size_t m_triangle_vert_ids_size = m_triangle_vert_ids.size();
        data.insert(
            data.end(),
            reinterpret_cast<const std::byte *>(&m_triangle_vert_ids_size),
            reinterpret_cast<const std::byte *>(&m_triangle_vert_ids_size + 1));
        data.insert(
            data.end(),
            reinterpret_cast<const std::byte *>(m_triangle_vert_ids.data()),
            reinterpret_cast<const std::byte *>(m_triangle_vert_ids.data() + m_triangle_vert_ids.size()));

        size_t m_triangle_normal_ids_size = m_triangle_normal_ids.size();
        data.insert(
            data.end(),
            reinterpret_cast<const std::byte *>(&m_triangle_normal_ids_size),
            reinterpret_cast<const std::byte *>(&m_triangle_normal_ids_size + 1));
        data.insert(
            data.end(),
            reinterpret_cast<const std::byte *>(m_triangle_normal_ids.data()),
            reinterpret_cast<const std::byte *>(m_triangle_normal_ids.data() + m_triangle_normal_ids.size()));

        size_t m_triangle_uv_ids_size = m_triangle_uv_ids.size();
        data.insert(
            data.end(),
            reinterpret_cast<const std::byte *>(&m_triangle_uv_ids_size),
            reinterpret_cast<const std::byte *>(&m_triangle_uv_ids_size + 1));
        data.insert(
            data.end(),
            reinterpret_cast<const std::byte *>(m_triangle_uv_ids.data()),
            reinterpret_cast<const std::byte *>(m_triangle_uv_ids.data() + m_triangle_uv_ids.size()));

        size_t m_positions_size = m_positions.size();
        data.insert(
            data.end(),
            reinterpret_cast<const std::byte *>(&m_positions_size),
            reinterpret_cast<const std::byte *>(&m_positions_size + 1));
        data.insert(
            data.end(),
            reinterpret_cast<const std::byte *>(m_positions.data()),
            reinterpret_cast<const std::byte *>(m_positions.data() + m_positions.size()));

        size_t m_normals_size = m_normals.size();
        data.insert(
            data.end(),
            reinterpret_cast<const std::byte *>(&m_normals_size),
            reinterpret_cast<const std::byte *>(&m_normals_size + 1));
        data.insert(
            data.end(),
            reinterpret_cast<const std::byte *>(m_normals.data()),
            reinterpret_cast<const std::byte *>(m_normals.data() + m_normals.size()));

        size_t m_uvs_size = m_uvs.size();
        data.insert(
            data.end(),
            reinterpret_cast<const std::byte *>(&m_uvs_size),
            reinterpret_cast<const std::byte *>(&m_uvs_size + 1));
        data.insert(
            data.end(),
            reinterpret_cast<const std::byte *>(m_uvs.data()),
            reinterpret_cast<const std::byte *>(m_uvs.data() + m_uvs.size()));
    }

    void MeshAsset::load_asset_from_archive(Serialization::Archive &archive)
    {
        Asset::load_asset_from_archive(archive);

        auto &data = archive.m_context->extra_data;
        size_t offset = 0;

        size_t m_offsets_size = *reinterpret_cast<const size_t *>(&data[offset]);
        offset += sizeof(size_t);
        m_offsets.resize(m_offsets_size);
        std::memcpy(m_offsets.data(), &data[offset], sizeof(size_t) * m_offsets_size);
        offset += sizeof(size_t) * m_offsets_size;
        
        size_t m_triangle_vert_ids_size = *reinterpret_cast<const size_t *>(&data[offset]);
        offset += sizeof(size_t);
        m_triangle_vert_ids.resize(m_triangle_vert_ids_size);
        std::memcpy(m_triangle_vert_ids.data(), &data[offset], sizeof(size_t) * m_triangle_vert_ids_size);
        offset += sizeof(size_t) * m_triangle_vert_ids_size;

        size_t m_triangle_normal_ids_size = *reinterpret_cast<const size_t *>(&data[offset]);
        offset += sizeof(size_t);
        m_triangle_normal_ids.resize(m_triangle_normal_ids_size);
        std::memcpy(m_triangle_normal_ids.data(), &data[offset], sizeof(size_t) * m_triangle_normal_ids_size);
        offset += sizeof(size_t) * m_triangle_normal_ids_size;

        size_t m_triangle_uv_ids_size = *reinterpret_cast<const size_t *>(&data[offset]);
        offset += sizeof(size_t);
        m_triangle_uv_ids.resize(m_triangle_uv_ids_size);
        std::memcpy(m_triangle_uv_ids.data(), &data[offset], sizeof(size_t) * m_triangle_uv_ids_size);
        offset += sizeof(size_t) * m_triangle_uv_ids_size;

        size_t m_positions_size = *reinterpret_cast<const size_t *>(&data[offset]);
        offset += sizeof(size_t);
        m_positions.resize(m_positions_size);
        std::memcpy(m_positions.data(), &data[offset], sizeof(float) * m_positions_size);
        offset += sizeof(float) * m_positions_size;

        size_t m_normals_size = *reinterpret_cast<const size_t *>(&data[offset]);
        offset += sizeof(size_t);
        m_normals.resize(m_normals_size);
        std::memcpy(m_normals.data(), &data[offset], sizeof(float) * m_normals_size);
        offset += sizeof(float) * m_normals_size;

        size_t m_uvs_size = *reinterpret_cast<const size_t *>(&data[offset]);
        offset += sizeof(size_t);
        m_uvs.resize(m_uvs_size);
        std::memcpy(m_uvs.data(), &data[offset], sizeof(float) * m_uvs_size);
        offset += sizeof(float) * m_uvs_size;
    }
}
