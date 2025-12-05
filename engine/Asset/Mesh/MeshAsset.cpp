#include "MeshAsset.h"
#include <Reflection/serialization.h>
#include <cassert>
#include <fstream>
#include <map>

namespace Engine {
    MeshAsset::MeshAsset() {
    }

    MeshAsset::~MeshAsset() {
    }

    size_t MeshAsset::GetSubmeshCount() const {
        return m_submeshes.size();
    }

    uint32_t MeshAsset::GetSubmeshVertexIndexCount(size_t submesh_idx) const {
        return m_submeshes[submesh_idx].m_indices.size();
    }
    uint32_t MeshAsset::GetSubmeshVertexCount(size_t submesh_idx) const {
        return m_submeshes[submesh_idx].vertex_count;
    }
    uint64_t MeshAsset::GetSubmeshExpectedBufferSize(size_t submesh_idx) const {
        uint64_t ret = GetSubmeshVertexIndexCount(submesh_idx) * sizeof(uint32_t);
        uint64_t attribute_size;

        attribute_size = m_submeshes[submesh_idx].positions.attribf.size();
        assert(Submesh::Attributes::GetStride(m_submeshes[submesh_idx].positions.type) == 0 
            || attribute_size / Submesh::Attributes::GetStride(m_submeshes[submesh_idx].positions.type) == GetSubmeshVertexCount(submesh_idx));
        ret += sizeof(uint32_t) * attribute_size;

        attribute_size = m_submeshes[submesh_idx].color.attribf.size();
        assert(Submesh::Attributes::GetStride(m_submeshes[submesh_idx].color.type) == 0 
            || attribute_size / Submesh::Attributes::GetStride(m_submeshes[submesh_idx].color.type) == GetSubmeshVertexCount(submesh_idx));
        ret += sizeof(uint32_t) * attribute_size;

        attribute_size = m_submeshes[submesh_idx].normal.attribf.size();
        assert(Submesh::Attributes::GetStride(m_submeshes[submesh_idx].normal.type) == 0 
            || attribute_size / Submesh::Attributes::GetStride(m_submeshes[submesh_idx].normal.type) == GetSubmeshVertexCount(submesh_idx));
        ret += sizeof(uint32_t) * attribute_size;

        attribute_size = m_submeshes[submesh_idx].texcoord0.attribf.size();
        assert(Submesh::Attributes::GetStride(m_submeshes[submesh_idx].texcoord0.type) == 0 
            || attribute_size / Submesh::Attributes::GetStride(m_submeshes[submesh_idx].texcoord0.type) == GetSubmeshVertexCount(submesh_idx));
        ret += sizeof(uint32_t) * attribute_size;

        attribute_size = m_submeshes[submesh_idx].tangent.attribf.size();
        assert(Submesh::Attributes::GetStride(m_submeshes[submesh_idx].tangent.type) == 0 
            || attribute_size / Submesh::Attributes::GetStride(m_submeshes[submesh_idx].tangent.type) == GetSubmeshVertexCount(submesh_idx));
        ret += sizeof(uint32_t) * attribute_size;

        attribute_size = m_submeshes[submesh_idx].texcoord1.attribf.size();
        assert(Submesh::Attributes::GetStride(m_submeshes[submesh_idx].texcoord1.type) == 0 
            || attribute_size / Submesh::Attributes::GetStride(m_submeshes[submesh_idx].texcoord1.type) == GetSubmeshVertexCount(submesh_idx));
        ret += sizeof(uint32_t) * attribute_size;

        attribute_size = m_submeshes[submesh_idx].texcoord2.attribf.size();
        assert(Submesh::Attributes::GetStride(m_submeshes[submesh_idx].texcoord2.type) == 0 
            || attribute_size / Submesh::Attributes::GetStride(m_submeshes[submesh_idx].texcoord2.type) == GetSubmeshVertexCount(submesh_idx));
        ret += sizeof(uint32_t) * attribute_size;

        attribute_size = m_submeshes[submesh_idx].texcoord3.attribf.size();
        assert(Submesh::Attributes::GetStride(m_submeshes[submesh_idx].texcoord3.type) == 0 
            || attribute_size / Submesh::Attributes::GetStride(m_submeshes[submesh_idx].texcoord3.type) == GetSubmeshVertexCount(submesh_idx));
        ret += sizeof(uint32_t) * attribute_size;

        attribute_size = m_submeshes[submesh_idx].bone_indices.attribf.size();
        assert(Submesh::Attributes::GetStride(m_submeshes[submesh_idx].bone_indices.type) == 0 
            || attribute_size / Submesh::Attributes::GetStride(m_submeshes[submesh_idx].bone_indices.type) == GetSubmeshVertexCount(submesh_idx));
        ret += sizeof(uint32_t) * attribute_size;

        attribute_size = m_submeshes[submesh_idx].bone_weights.attribf.size();
        assert(Submesh::Attributes::GetStride(m_submeshes[submesh_idx].bone_weights.type) == 0 
            || attribute_size / Submesh::Attributes::GetStride(m_submeshes[submesh_idx].bone_weights.type) == GetSubmeshVertexCount(submesh_idx));
        ret += sizeof(uint32_t) * attribute_size;

        return ret;
    }

    void MeshAsset::save_asset_to_archive(Serialization::Archive &archive) const {
        auto &json = *archive.m_cursor;
        size_t extra_data_id = archive.create_new_extra_data_buffer(".mesh");
        json["%extra_data_id"] = extra_data_id;
        auto &data = archive.m_context->extra_data[extra_data_id];

        size_t reserved_size = sizeof(size_t); // submesh count
        size_t submesh_count = GetSubmeshCount();
        for (size_t i = 0; i < submesh_count; i++) {
            reserved_size +=
                sizeof(size_t) * 2
                + GetSubmeshExpectedBufferSize(i); // size of indices + size of vertex + all indices and vertex data
        }
        data.reserve(reserved_size);

        data.insert(
            data.end(),
            reinterpret_cast<const std::byte *>(&submesh_count),
            reinterpret_cast<const std::byte *>((&submesh_count) + 1)
        );
        for (size_t i = 0; i < submesh_count; i++) {
            size_t m_indices_size = m_submeshes[i].m_indices.size();
            data.insert(
                data.end(),
                reinterpret_cast<const std::byte *>(&m_indices_size),
                reinterpret_cast<const std::byte *>((&m_indices_size) + 1)
            );
            data.insert(
                data.end(),
                reinterpret_cast<const std::byte *>(m_submeshes[i].m_indices.data()),
                reinterpret_cast<const std::byte *>(m_submeshes[i].m_indices.data() + m_submeshes[i].m_indices.size())
            );

            size_t m_vertex_size = GetSubmeshVertexCount(i);
            data.insert(
                data.end(),
                reinterpret_cast<const std::byte *>(&m_vertex_size),
                reinterpret_cast<const std::byte *>((&m_vertex_size) + 1)
            );
            /* TODO: serialize vertex attributes */
        }

        // save base class (such as GUID)
        Asset::save_asset_to_archive(archive);
    }

    void MeshAsset::load_asset_from_archive(Serialization::Archive &archive) {
        auto &json = *archive.m_cursor;
        auto &data = archive.m_context->extra_data[json["%extra_data_id"].get<size_t>()];
        size_t offset = 0;

        size_t submesh_count = *reinterpret_cast<const size_t *>(&data[offset]);
        offset += sizeof(size_t);
        m_submeshes.resize(submesh_count);
        for (size_t i = 0; i < submesh_count; i++) {
            size_t m_indices_size = *reinterpret_cast<const size_t *>(&data[offset]);
            offset += sizeof(size_t);
            m_submeshes[i].m_indices.resize(m_indices_size);
            std::memcpy(
                m_submeshes[i].m_indices.data(),
                &data[offset],
                m_indices_size * sizeof(decltype(m_submeshes[i].m_indices)::value_type)
            );
            offset += m_indices_size * sizeof(decltype(m_submeshes[i].m_indices)::value_type);

            size_t m_vertex_size = *reinterpret_cast<const size_t *>(&data[offset]);
            offset += sizeof(size_t);
            /* TODO: deserialize vertex attributes */
        }

        Asset::load_asset_from_archive(archive);
    }
} // namespace Engine
