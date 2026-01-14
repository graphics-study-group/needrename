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
        // Base size: indices buffer
        uint64_t ret = GetSubmeshVertexIndexCount(submesh_idx) * sizeof(uint32_t);

        const auto &sm = m_submeshes[submesh_idx];
        const auto add_attr = [&](const Submesh::Attributes &attr) {
            // type header + element count header
            ret += sizeof(uint32_t) + sizeof(size_t);
            using T = Submesh::Attributes::AttributeType;
            switch (attr.type) {
            case T::Floatx1:
            case T::Floatx2:
            case T::Floatx3:
            case T::Floatx4: {
                auto attribute_size = attr.attribf.size();
                if (Submesh::Attributes::GetStride(attr.type) != 0) {
                    assert(attribute_size / Submesh::Attributes::GetStride(attr.type) == GetSubmeshVertexCount(submesh_idx));
                }
                ret += static_cast<uint64_t>(attribute_size) * sizeof(uint32_t);
                break;
            }
            case T::Uintx1:
            case T::Uintx2:
            case T::Uintx3:
            case T::Uintx4: {
                auto attribute_size = attr.attribu.size();
                if (Submesh::Attributes::GetStride(attr.type) != 0) {
                    assert(attribute_size / Submesh::Attributes::GetStride(attr.type) == GetSubmeshVertexCount(submesh_idx));
                }
                ret += static_cast<uint64_t>(attribute_size) * sizeof(uint32_t);
                break;
            }
            default:
                // Unused: only headers
                break;
            }
        };

        add_attr(sm.positions);
        add_attr(sm.color);
        add_attr(sm.normal);
        add_attr(sm.texcoord0);
        add_attr(sm.tangent);
        add_attr(sm.texcoord1);
        add_attr(sm.texcoord2);
        add_attr(sm.texcoord3);
        add_attr(sm.bone_indices);
        add_attr(sm.bone_weights);

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
            // indices_count header + vertex_count header + payload size estimate
            reserved_size += sizeof(size_t) * 2 + GetSubmeshExpectedBufferSize(i);
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
            // serialize vertex attributes (type, element count, raw data)
            const auto write_attr = [&](const Submesh::Attributes &attr) {
                auto type_u32 = static_cast<uint32_t>(attr.type);
                data.insert(
                    data.end(),
                    reinterpret_cast<const std::byte *>(&type_u32),
                    reinterpret_cast<const std::byte *>((&type_u32) + 1)
                );

                using T = Submesh::Attributes::AttributeType;
                size_t elem_count = 0;
                switch (attr.type) {
                case T::Floatx1:
                case T::Floatx2:
                case T::Floatx3:
                case T::Floatx4:
                    elem_count = attr.attribf.size();
                    break;
                case T::Uintx1:
                case T::Uintx2:
                case T::Uintx3:
                case T::Uintx4:
                    elem_count = attr.attribu.size();
                    break;
                default:
                    elem_count = 0;
                    break;
                }

                data.insert(
                    data.end(),
                    reinterpret_cast<const std::byte *>(&elem_count),
                    reinterpret_cast<const std::byte *>((&elem_count) + 1)
                );

                if (elem_count == 0) return;
                if (attr.type == T::Floatx1 || attr.type == T::Floatx2 || attr.type == T::Floatx3 || attr.type == T::Floatx4) {
                    data.insert(
                        data.end(),
                        reinterpret_cast<const std::byte *>(attr.attribf.data()),
                        reinterpret_cast<const std::byte *>(attr.attribf.data() + attr.attribf.size())
                    );
                } else if (attr.type == T::Uintx1 || attr.type == T::Uintx2 || attr.type == T::Uintx3 || attr.type == T::Uintx4) {
                    data.insert(
                        data.end(),
                        reinterpret_cast<const std::byte *>(attr.attribu.data()),
                        reinterpret_cast<const std::byte *>(attr.attribu.data() + attr.attribu.size())
                    );
                }
            };

            write_attr(m_submeshes[i].positions);
            write_attr(m_submeshes[i].color);
            write_attr(m_submeshes[i].normal);
            write_attr(m_submeshes[i].texcoord0);
            write_attr(m_submeshes[i].tangent);
            write_attr(m_submeshes[i].texcoord1);
            write_attr(m_submeshes[i].texcoord2);
            write_attr(m_submeshes[i].texcoord3);
            write_attr(m_submeshes[i].bone_indices);
            write_attr(m_submeshes[i].bone_weights);
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
            m_submeshes[i].vertex_count = static_cast<uint32_t>(m_vertex_size);

            const auto read_attr = [&](Submesh::Attributes &attr) {
                using T = Submesh::Attributes::AttributeType;
                uint32_t type_u32 = *reinterpret_cast<const uint32_t *>(&data[offset]);
                offset += sizeof(uint32_t);
                attr.type = static_cast<T>(type_u32);

                size_t elem_count = *reinterpret_cast<const size_t *>(&data[offset]);
                offset += sizeof(size_t);

                if (elem_count == 0) {
                    attr.attribf.clear();
                    attr.attribu.clear();
                    return;
                }

                switch (attr.type) {
                case T::Floatx1:
                case T::Floatx2:
                case T::Floatx3:
                case T::Floatx4: {
                    attr.attribf.resize(elem_count);
                    std::memcpy(attr.attribf.data(), &data[offset], elem_count * sizeof(float));
                    offset += elem_count * sizeof(float);
                    break;
                }
                case T::Uintx1:
                case T::Uintx2:
                case T::Uintx3:
                case T::Uintx4: {
                    attr.attribu.resize(elem_count);
                    std::memcpy(attr.attribu.data(), &data[offset], elem_count * sizeof(uint32_t));
                    offset += elem_count * sizeof(uint32_t);
                    break;
                }
                default:
                    // Unused: nothing to read
                    break;
                }
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
        }

        Asset::load_asset_from_archive(archive);
    }
} // namespace Engine
