#include "MeshAsset.h"

#include <SDL3/SDL.h>

#include "Render/Pipeline/Renderer/VertexAttribute.h"
#include <AnnoRefl/serialization.h>

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
#include "MeshAsset.h"

#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <cstring>

    namespace {
        /**
         * @brief Write a fixed-width value into a byte buffer, little endian.
         *
         * The mesh archive used to dump `size_t` and whole `Attributes` structs, which made the
         * file a picture of one build: a 64-bit writer and a 32-bit reader disagree about the
         * width of a size and about the padding inside a struct, and the reader then saw a vertex
         * buffer of zero bytes with the attribute offsets of the other build. Every field is
         * written at a stated width here so the same file means the same thing on both.
         */
        template <typename T>
        void WriteLittleEndian(std::vector<std::byte> &data, T value) {
            static_assert(std::is_integral_v<T>);
            using Unsigned = std::make_unsigned_t<T>;
            const auto bits = static_cast<Unsigned>(value);
            for (size_t byte = 0; byte < sizeof(T); byte++) {
                data.push_back(static_cast<std::byte>((bits >> (byte * 8u)) & static_cast<Unsigned>(0xFF)));
            }
        }

        /// @brief Read a fixed-width value written by @ref WriteLittleEndian.
        template <typename T>
        bool ReadLittleEndian(const std::vector<std::byte> &data, size_t &offset, T &value) {
            static_assert(std::is_integral_v<T>);
            if (offset + sizeof(T) > data.size()) {
                return false;
            }
            using Unsigned = std::make_unsigned_t<T>;
            Unsigned bits = 0;
            for (size_t byte = 0; byte < sizeof(T); byte++) {
                bits |= static_cast<Unsigned>(std::to_integer<Unsigned>(data[offset + byte]))
                        << (byte * 8u);
            }
            offset += sizeof(T);
            value = static_cast<T>(bits);
            return true;
        }

        /// @brief Write the host-endian payload bytes of a buffer as they are.
        void WriteBytes(std::vector<std::byte> &data, const void *source, size_t size) {
            const auto *bytes = static_cast<const std::byte *>(source);
            data.insert(data.end(), bytes, bytes + size);
        }
    } // namespace

    void MeshAsset::save_asset_to_archive(AnnoRefl::Archive &archive) const {
        auto &json = *archive.m_cursor;
        size_t extra_data_id = archive.create_new_extra_data_buffer(".mesh");
        json["%extra_data_id"] = extra_data_id;
        auto &data = archive.m_context->extra_data[extra_data_id];

        constexpr uint32_t k_format_version = 2;
        uint64_t submesh_count = m_submeshes.size();
        WriteLittleEndian(data, k_format_version);
        WriteLittleEndian(data, submesh_count);
        for (const Submesh &sm : m_submeshes) {
            WriteLittleEndian(data, static_cast<uint64_t>(sm.m_indices.size()));
            WriteLittleEndian(data, static_cast<uint64_t>(sm.vertex_count));
            WriteLittleEndian(data, static_cast<uint64_t>(sm.m_vertex_attributes.size()));

            const auto write_attr = [&data](const Submesh::Attributes &attr) {
                WriteLittleEndian(data, static_cast<uint32_t>(attr.type));
                WriteLittleEndian(data, static_cast<uint64_t>(attr.buffer_offset));
                WriteLittleEndian(data, static_cast<uint64_t>(attr.buffer_size));
            };

            // The write order matches the read order and the semantic order in
            // `WriteVertexAttributeBuffer`.
            write_attr(sm.positions);
            write_attr(sm.color);
            write_attr(sm.normal);
            write_attr(sm.tangent);
            write_attr(sm.texcoord0);
            write_attr(sm.texcoord1);
            write_attr(sm.texcoord2);
            write_attr(sm.texcoord3);
            write_attr(sm.bone_indices);
            write_attr(sm.bone_weights);

            // Then the payloads, indices first.
            WriteBytes(data, sm.m_indices.data(), sm.m_indices.size() * sizeof(uint32_t));
            WriteBytes(data, sm.m_vertex_attributes.data(), sm.m_vertex_attributes.size());
        }

        // save base class (such as GUID)
        Asset::save_asset_to_archive(archive);
    }

    bool MeshAsset::LoadLegacyMesh(const std::vector<std::byte> &data) {
        // Two layouts are possible: the writer's `size_t` was 8 bytes or 4. Both are parsed, and
        // the one that consumes the buffer exactly is the writer's own.
        for (const size_t width : {size_t{8}, size_t{4}}) {
            const auto read_size = [&data, width](size_t &offset, uint64_t &value) {
                if (offset + width > data.size()) {
                    return false;
                }
                uint64_t result = 0;
                for (size_t byte = 0; byte < width; byte++) {
                    result |= static_cast<uint64_t>(std::to_integer<uint8_t>(data[offset + byte]))
                              << (byte * 8u);
                }
                offset += width;
                value = result;
                return true;
            };

            std::vector<Submesh> parsed;
            size_t offset = 0;
            uint64_t submesh_count = 0;
            bool ok = read_size(offset, submesh_count);
            // A count larger than the buffer could possibly describe is not this width's layout.
            ok = ok && submesh_count <= data.size();
            if (ok) {
                parsed.resize(static_cast<size_t>(submesh_count));
            }
            for (size_t i = 0; ok && i < parsed.size(); i++) {
                uint64_t index_count = 0;
                uint64_t vertex_count = 0;
                uint64_t vertex_buffer_size = 0;
                ok = read_size(offset, index_count) && read_size(offset, vertex_count)
                     && read_size(offset, vertex_buffer_size);
                if (!ok) {
                    break;
                }
                parsed[i].vertex_count = static_cast<uint32_t>(vertex_count);

                // The older writer listed `texcoord0` before `tangent`; the fixed-width format
                // lists them in `VertexAttributeSemantic` order, which puts `tangent` first. An
                // archive in the old order has to be read in the old order too, or the UV stream
                // is taken for a `vec2` tangent and the tangent for a `vec4` UV. That is not a
                // cosmetic mix-up: the lit pass builds its TBN from the tangent and samples the
                // normal map through the UV, so a swapped pair shades the surface black with thin
                // lit slivers, and only on assets archived before the format changed.
                Submesh::Attributes *const attributes[10] = {
                    &parsed[i].positions, &parsed[i].color,      &parsed[i].normal,       &parsed[i].texcoord0,
                    &parsed[i].tangent,   &parsed[i].texcoord1,  &parsed[i].texcoord2,    &parsed[i].texcoord3,
                    &parsed[i].bone_indices, &parsed[i].bone_weights
                };
                for (Submesh::Attributes *const attribute : attributes) {
                    // The struct was written whole: the enum, then two `size_t`s, then padding to
                    // the struct's alignment, which is the width itself.
                    uint64_t type = 0;
                    uint64_t buffer_offset = 0;
                    uint64_t buffer_size = 0;
                    if (offset + sizeof(uint32_t) > data.size()) {
                        ok = false;
                        break;
                    }
                    std::memcpy(&type, &data[offset], sizeof(uint32_t));
                    offset += width == 8 ? 8 : 4; // the enum, padded to the width of a size_t
                    ok = read_size(offset, buffer_offset) && read_size(offset, buffer_size);
                    if (!ok) {
                        break;
                    }
                    attribute->type = static_cast<VertexAttributeType>(static_cast<uint32_t>(type));
                    attribute->buffer_offset = static_cast<size_t>(buffer_offset);
                    attribute->buffer_size = static_cast<size_t>(buffer_size);
                }
                if (!ok) {
                    break;
                }

                const size_t index_bytes = static_cast<size_t>(index_count) * sizeof(uint32_t);
                if (index_bytes > data.size() - offset
                    || vertex_buffer_size > data.size() - offset - index_bytes) {
                    ok = false;
                    break;
                }
                parsed[i].m_indices.resize(static_cast<size_t>(index_count));
                std::memcpy(parsed[i].m_indices.data(), &data[offset], index_bytes);
                offset += index_bytes;
                parsed[i].m_vertex_attributes.resize(static_cast<size_t>(vertex_buffer_size));
                std::memcpy(parsed[i].m_vertex_attributes.data(), &data[offset], vertex_buffer_size);
                offset += static_cast<size_t>(vertex_buffer_size);
            }

            // The payload is last, so a layout that is right ends exactly at the end of the buffer.
            if (ok && offset == data.size()) {
                m_submeshes = std::move(parsed);
                return true;
            }
        }
        return false;
    }

    void MeshAsset::load_asset_from_archive(AnnoRefl::Archive &archive) {
        auto &json = *archive.m_cursor;
        auto &data = archive.m_context->extra_data[json["%extra_data_id"].get<size_t>()];
        size_t offset = 0;

        uint32_t format_version = 0;
        if (!ReadLittleEndian(data, offset, format_version) || format_version != 2) {
            // An archive in the older form, whose fields were written at the writer's own widths.
            // The shipped projects still hold such assets, and a 32-bit reader cannot take one at
            // face value: the same file written by a 64-bit build reads as "vertex buffer 0 bytes"
            // and "index count 36" here. The width is recovered instead - both are tried, and the
            // one whose payload ends exactly at the end of the buffer is the one that wrote it.
            if (LoadLegacyMesh(data)) {
                Asset::load_asset_from_archive(archive);
                return;
            }
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER,
                "MeshAsset: the archived mesh is neither the current format nor a readable older "
                "one; the asset stays empty until it is imported again."
            );
            m_submeshes.clear();
            Asset::load_asset_from_archive(archive);
            return;
        }
        uint64_t submesh_count = 0;
        if (!ReadLittleEndian(data, offset, submesh_count)) {
            submesh_count = 0;
        }
        m_submeshes.resize(submesh_count);
        for (size_t i = 0; i < m_submeshes.size(); i++) {
            uint64_t index_count = 0;
            uint64_t vertex_count = 0;
            uint64_t vertex_buffer_size = 0;
            const bool header_ok = ReadLittleEndian(data, offset, index_count)
                                   && ReadLittleEndian(data, offset, vertex_count)
                                   && ReadLittleEndian(data, offset, vertex_buffer_size);
            if (!header_ok) {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "MeshAsset: the archived mesh ended early.");
                m_submeshes.clear();
                break;
            }

            m_submeshes[i].vertex_count = static_cast<uint32_t>(vertex_count);

            const auto read_attr = [&data, &offset](Submesh::Attributes &attr) {
                uint32_t type = 0;
                uint64_t buffer_offset = 0;
                uint64_t buffer_size = 0;
                if (!ReadLittleEndian(data, offset, type) || !ReadLittleEndian(data, offset, buffer_offset)
                    || !ReadLittleEndian(data, offset, buffer_size)) {
                    return false;
                }
                attr.type = static_cast<VertexAttributeType>(type);
                attr.buffer_offset = static_cast<size_t>(buffer_offset);
                attr.buffer_size = static_cast<size_t>(buffer_size);
                return true;
            };

            struct AttributeSlot {
                Submesh::Attributes *attribute;
            };
            const std::array<AttributeSlot, 10> slots = {
                AttributeSlot{&m_submeshes[i].positions},    AttributeSlot{&m_submeshes[i].color},
                AttributeSlot{&m_submeshes[i].normal},       AttributeSlot{&m_submeshes[i].tangent},
                AttributeSlot{&m_submeshes[i].texcoord0},    AttributeSlot{&m_submeshes[i].texcoord1},
                AttributeSlot{&m_submeshes[i].texcoord2},    AttributeSlot{&m_submeshes[i].texcoord3},
                AttributeSlot{&m_submeshes[i].bone_indices}, AttributeSlot{&m_submeshes[i].bone_weights}
            };
            for (const AttributeSlot &slot : slots) {
                if (!read_attr(*slot.attribute)) {
                    SDL_LogError(SDL_LOG_CATEGORY_RENDER, "MeshAsset: the archived mesh ended early.");
                    m_submeshes.clear();
                    break;
                }
            }

            const size_t index_bytes = static_cast<size_t>(index_count) * sizeof(uint32_t);
            if (offset + index_bytes + vertex_buffer_size > data.size()) {
                SDL_LogError(
                    SDL_LOG_CATEGORY_RENDER,
                    "MeshAsset: the archived mesh claims %zu index bytes and %zu vertex bytes where "
                    "the archive holds %zu.",
                    index_bytes,
                    static_cast<size_t>(vertex_buffer_size),
                    data.size() - offset
                );
                m_submeshes.clear();
                break;
            }

            m_submeshes[i].m_indices.resize(static_cast<size_t>(index_count));
            std::memcpy(m_submeshes[i].m_indices.data(), &data[offset], index_bytes);
            offset += index_bytes;

            m_submeshes[i].m_vertex_attributes.resize(static_cast<size_t>(vertex_buffer_size));
            std::memcpy(m_submeshes[i].m_vertex_attributes.data(), &data[offset], vertex_buffer_size);
            offset += vertex_buffer_size;
        }

        Asset::load_asset_from_archive(archive);
    }
    VertexAttribute MeshAsset::Submesh::ToVertexAttributeFormat() const noexcept {
#ifdef SET_ATTRIBUTE_TYPE
#error Duplicated SET_ATTRIBUTE_TYPE definition
#else
#define SET_ATTRIBUTE_TYPE(attr, semantics) ret.SetAttribute(VertexAttributeSemantic::semantics, attr.type)
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
            // The attribute table and the packed buffer have to agree. They are two separate
            // pieces of data - one written by the importer, one by whoever loaded or copied the
            // submesh - and a mismatch used to be a copy out of whatever address the sum landed
            // on. Saying so is cheaper than finding out later.
            const bool in_range = attr.buffer_size == 0
                                  || (attr.buffer_offset <= m_vertex_attributes.size()
                                      && attr.buffer_size <= m_vertex_attributes.size() - attr.buffer_offset);
            if (!in_range) {
                SDL_LogError(
                    SDL_LOG_CATEGORY_RENDER,
                    "MeshAsset: attribute at offset %zu with %zu bytes is outside the packed vertex "
                    "buffer of %zu bytes (vertex count %u); the copy is skipped.",
                    attr.buffer_offset,
                    attr.buffer_size,
                    m_vertex_attributes.size(),
                    vertex_count
                );
                current_offset += attr.buffer_size;
                return;
            }
            std::memcpy(buf + current_offset, m_vertex_attributes.data() + attr.buffer_offset, attr.buffer_size);
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

#include "__generated__/MeshAsset.h.inc"