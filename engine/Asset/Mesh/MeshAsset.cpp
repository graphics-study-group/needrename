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
        return m_submeshes[submesh_idx].m_positions.size();
    }
    uint64_t MeshAsset::GetSubmeshExpectedBufferSize(size_t submesh_idx) const {
        uint64_t ret = GetSubmeshVertexIndexCount(submesh_idx) * sizeof(uint32_t);
        if (!m_submeshes[submesh_idx].m_attributes_basic.empty()) {
            ret += GetSubmeshVertexCount(submesh_idx) * sizeof(VertexStruct::VertexAttributeBasic);
        }
        if (!m_submeshes[submesh_idx].m_attributes_extended.empty()) {
            assert(
                m_submeshes[submesh_idx].m_attributes_extended.size()
                == m_submeshes[submesh_idx].m_attributes_basic.size()
            );
            ret += GetSubmeshVertexCount(submesh_idx) * sizeof(VertexStruct::VertexAttributeExtended);
        }
        if (!m_submeshes[submesh_idx].m_attributes_skinned.empty()) {
            assert(
                m_submeshes[submesh_idx].m_attributes_skinned.size()
                == m_submeshes[submesh_idx].m_attributes_basic.size()
            );
            ret += GetSubmeshVertexCount(submesh_idx) * sizeof(VertexStruct::VertexAttributeSkinned);
        }
        return ret;
    }

    void MeshAsset::save_asset_to_archive(Serialization::Archive &archive) const {
        auto &data = archive.m_context->extra_data;
        assert(data.empty());

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

            size_t m_vertex_size = m_submeshes[i].m_positions.size();
            data.insert(
                data.end(),
                reinterpret_cast<const std::byte *>(&m_vertex_size),
                reinterpret_cast<const std::byte *>((&m_vertex_size) + 1)
            );
            data.insert(
                data.end(),
                reinterpret_cast<const std::byte *>(m_submeshes[i].m_positions.data()),
                reinterpret_cast<const std::byte *>(
                    m_submeshes[i].m_positions.data() + m_submeshes[i].m_positions.size()
                )
            );
            data.insert(
                data.end(),
                reinterpret_cast<const std::byte *>(m_submeshes[i].m_attributes_basic.data()),
                reinterpret_cast<const std::byte *>(
                    m_submeshes[i].m_attributes_basic.data() + m_submeshes[i].m_attributes_basic.size()
                )
            );
        }

        // save base class (such as GUID)
        Asset::save_asset_to_archive(archive);
    }

    void MeshAsset::load_asset_from_archive(Serialization::Archive &archive) {
        auto &data = archive.m_context->extra_data;
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
            m_submeshes[i].m_positions.resize(m_vertex_size);
            std::memcpy(
                m_submeshes[i].m_positions.data(),
                &data[offset],
                m_vertex_size * sizeof(decltype(m_submeshes[i].m_positions)::value_type)
            );
            offset += m_vertex_size * sizeof(decltype(m_submeshes[i].m_positions)::value_type);
            m_submeshes[i].m_attributes_basic.resize(m_vertex_size);
            std::memcpy(
                m_submeshes[i].m_attributes_basic.data(),
                &data[offset],
                m_vertex_size * sizeof(decltype(m_submeshes[i].m_attributes_basic)::value_type)
            );
            offset += m_vertex_size * sizeof(decltype(m_submeshes[i].m_attributes_basic)::value_type);
        }

        Asset::load_asset_from_archive(archive);
    }
} // namespace Engine
