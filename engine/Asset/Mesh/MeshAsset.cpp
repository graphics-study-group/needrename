#include "MeshAsset.h"

#include "Render/Renderer/VertexAttribute.h"
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

    void MeshAsset::save_asset_to_archive(Serialization::Archive &archive) const {
        auto &json = *archive.m_cursor;
        size_t extra_data_id = archive.create_new_extra_data_buffer(".mesh");
        json["%extra_data_id"] = extra_data_id;
        auto &data = archive.m_context->extra_data[extra_data_id];

        size_t reserved_size = sizeof(size_t); // submesh count
        size_t submesh_count = GetSubmeshCount();
        for (size_t i = 0; i < submesh_count; i++) {
            // indices_count header + vertex_count header + total vertex attribute buffer size
            reserved_size += sizeof(size_t) + sizeof(size_t) + sizeof(size_t);
            // metadata for each attribute
            reserved_size += (sizeof(Submesh::Attributes)) * 16;
            // payload size estimate
            reserved_size += m_submeshes[i].GetTotalBufferSize();
        }
        data.reserve(reserved_size);

        data.insert(
            data.end(),
            reinterpret_cast<const std::byte *>(&submesh_count),
            reinterpret_cast<const std::byte *>((&submesh_count) + 1)
        );
        for (size_t i = 0; i < submesh_count; i++) {
            const auto & sm = m_submeshes[i];
            // Meta data goes first
            std::array<size_t, 3> sz = {sm.m_indices.size(), sm.vertex_count, sm.m_vertex_attributes.size()};
            data.insert(
                data.end(),
                reinterpret_cast<const std::byte *>(sz.data()),
                reinterpret_cast<const std::byte *>(sz.data() + sz.size())
            );

            auto write_attr = [&data](const Submesh::Attributes & attr) -> void {
                static_assert(std::is_trivially_copyable_v<Submesh::Attributes>);
                data.insert(
                    data.end(),
                    reinterpret_cast<const std::byte *>(&attr),
                    reinterpret_cast<const std::byte *>((&attr) + 1)
                );
            };

            write_attr(sm.positions);
            write_attr(sm.color);
            write_attr(sm.normal);
            write_attr(sm.texcoord0);
            write_attr(sm.tangent);
            write_attr(sm.texcoord1);
            write_attr(sm.texcoord2);
            write_attr(sm.texcoord3);
            write_attr(sm.bone_indices);
            write_attr(sm.bone_weights);

            // Then actual buffers
            data.insert(
                data.end(),
                reinterpret_cast<const std::byte *>(sm.m_indices.data()),
                reinterpret_cast<const std::byte *>(sm.m_indices.data() + sm.m_indices.size())
            );
            data.insert(
                data.end(),
                sm.m_vertex_attributes.begin(),
                sm.m_vertex_attributes.end()
            );
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

            // First get index and vertex counts
            size_t index_size = *reinterpret_cast<const size_t *>(&data[offset]);
            offset += sizeof(size_t);
            size_t vertex_count = *reinterpret_cast<const size_t *>(&data[offset]);
            offset += sizeof(size_t);
            size_t vertex_buffer_size = *reinterpret_cast<const size_t *>(&data[offset]);
            offset += sizeof(size_t);

            m_submeshes[i].vertex_count = static_cast<uint32_t>(vertex_count);

            // Then read metadata
            const auto read_attr = [&](Submesh::Attributes &attr) {
                static_assert(std::is_trivially_copyable_v<Submesh::Attributes>);
                std::memcpy(
                    reinterpret_cast<std::byte *>(&attr),
                    &data[offset],
                    sizeof(decltype(attr))
                );
                offset += sizeof(decltype(attr));
            };

            read_attr(m_submeshes[i].positions);
            read_attr(m_submeshes[i].color);
            read_attr(m_submeshes[i].normal);
            read_attr(m_submeshes[i].texcoord0);
            read_attr(m_submeshes[i].tangent);
            read_attr(m_submeshes[i].texcoord1);
            read_attr(m_submeshes[i].texcoord2);
            read_attr(m_submeshes[i].texcoord3);
            read_attr(m_submeshes[i].bone_indices);
            read_attr(m_submeshes[i].bone_weights);

            // Finally copy payload buffers
            m_submeshes[i].m_indices.resize(index_size);
            std::memcpy(
                m_submeshes[i].m_indices.data(),
                &data[offset],
                index_size * sizeof(decltype(m_submeshes[i].m_indices[0]))
            );
            offset += index_size * sizeof(decltype(m_submeshes[i].m_indices[0]));

            m_submeshes[i].m_vertex_attributes.resize(vertex_buffer_size);
            std::memcpy(
                m_submeshes[i].m_vertex_attributes.data(),
                &data[offset],
                vertex_buffer_size
            );
            offset += vertex_buffer_size;
        }

        Asset::load_asset_from_archive(archive);
    }
    VertexAttribute MeshAsset::Submesh::ToVertexAttributeFormat() const noexcept {
#ifdef SET_ATTRIBUTE_TYPE
#error Duplicated SET_ATTRIBUTE_TYPE definition
#else
#define SET_ATTRIBUTE_TYPE(attr, semantics) \
        ret.SetAttribute(VertexAttributeSemantic::semantics, attr.type)
#endif
        VertexAttribute ret{};
        SET_ATTRIBUTE_TYPE(positions, Position);
        SET_ATTRIBUTE_TYPE(color, Color);
        SET_ATTRIBUTE_TYPE(normal, Normal);
        SET_ATTRIBUTE_TYPE(texcoord0, Texcoord0);
        SET_ATTRIBUTE_TYPE(tangent, Tangent);
        SET_ATTRIBUTE_TYPE(texcoord1, Texcoord1);
        SET_ATTRIBUTE_TYPE(texcoord2, Texcoord2);
        SET_ATTRIBUTE_TYPE(texcoord3, Texcoord3);
        SET_ATTRIBUTE_TYPE(bone_indices, BoneIndices);
        SET_ATTRIBUTE_TYPE(bone_weights, BoneWeights);
        return ret;
#undef SET_ATTRIBUTE_TYPE
    }
    void MeshAsset::Submesh::WriteVertexAttributeBuffer(std::byte *buf) const noexcept {
        uint64_t current_offset = 0;
        const auto write_attr = [&](const Submesh::Attributes &attr) {
            std::memcpy(
                buf + current_offset,
                m_vertex_attributes.data() + attr.buffer_offset,
                attr.buffer_size
            );
            current_offset += attr.buffer_size;
        };

        // Write order must matches `VertexAttributeSemantic` order.
        write_attr(positions);
        write_attr(color);
        write_attr(normal);
        write_attr(tangent);
        write_attr(texcoord0);
        write_attr(texcoord1);
        write_attr(texcoord2);
        write_attr(texcoord3);
        write_attr(bone_indices);
        write_attr(bone_weights);
    }
    void MeshAsset::Submesh::WriteIndexBuffer(std::byte *buf) const noexcept {
        std::memcpy(buf, m_indices.data(), sizeof(uint32_t) * m_indices.size());
    }
} // namespace Engine
