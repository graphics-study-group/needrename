#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "Render/Asset/Mesh/MeshAsset.h"

using namespace Engine;

namespace {
    /// @brief Number of failures reported so far.
    int failures = 0;

    void Check(bool condition, const std::string &what) {
        if (condition) return;
        std::cerr << "FAIL: " << what << '\n';
        failures++;
    }

    /// @brief Index count of the synthetic submesh.
    constexpr uint32_t k_index_count = 3;
    /// @brief Vertex count of the synthetic submesh.
    constexpr uint32_t k_vertex_count = 2;
    /// @brief Size of one submesh's `vec3` position stream.
    constexpr uint64_t k_position_bytes = k_vertex_count * 12;
    /// @brief Size of one submesh's `vec3` normal stream.
    constexpr uint64_t k_normal_bytes = k_vertex_count * 12;
    /// @brief Size of one submesh's `vec2` UV stream.
    constexpr uint64_t k_texcoord_bytes = k_vertex_count * 8;
    /// @brief Size of one submesh's `vec4` tangent stream.
    constexpr uint64_t k_tangent_bytes = k_vertex_count * 16;
    /// @brief Offset of the UV stream in the packed vertex buffer.
    constexpr uint64_t k_texcoord_offset = k_position_bytes + k_normal_bytes;
    /// @brief Offset of the tangent stream in the packed vertex buffer.
    constexpr uint64_t k_tangent_offset = k_texcoord_offset + k_texcoord_bytes;
    /// @brief Size of the packed vertex buffer.
    constexpr uint64_t k_vertex_buffer_bytes = k_tangent_offset + k_tangent_bytes;

    /// @brief Append a fixed-width little-endian value, as the legacy writer did for a `size_t`.
    void AppendLittleEndian(std::vector<std::byte> &data, uint64_t value, size_t width) {
        for (size_t byte = 0; byte < width; byte++) {
            data.push_back(static_cast<std::byte>((value >> (byte * 8u)) & 0xFFu));
        }
    }

    /// @brief One attribute record of a legacy mesh archive.
    struct AttributeSpec {
        uint32_t type;
        uint64_t buffer_offset;
        uint64_t buffer_size;
    };

    /**
     * @brief Build a one-submesh mesh archive in the pre-v2 layout.
     *
     * The legacy writer listed `texcoord0` before `tangent`, the opposite of the fixed-width
     * format, which follows `VertexAttributeSemantic` order. The vertex payload is laid out in
     * the same order the attribute table states, so a reader that assigns the slots in the wrong
     * order is detectable from the types alone: it would call the UV a `vec2` tangent and the
     * tangent a `vec4` UV.
     *
     * @param width Width of the writer's `size_t`, in bytes. Both the 8-byte and the 4-byte writer
     *              exist, and the reader has to recover which one it is looking at.
     * @return The archive payload.
     */
    std::vector<std::byte> MakeLegacyMesh(size_t width) {
        const AttributeSpec unused{0, 0, 0};
        const AttributeSpec position{3, 0, k_position_bytes};            // SFloat32x3
        const AttributeSpec normal{3, k_position_bytes, k_normal_bytes}; // SFloat32x3
        const AttributeSpec texcoord0{2, k_texcoord_offset, k_texcoord_bytes}; // SFloat32x2
        const AttributeSpec tangent{4, k_tangent_offset, k_tangent_bytes};     // SFloat32x4
        // Legacy write order: positions, color, normal, texcoord0, tangent, texcoord1..3, bones.
        const AttributeSpec attributes[10] = {
            position, unused, normal, texcoord0, tangent, unused, unused, unused, unused, unused
        };

        std::vector<std::byte> data;
        AppendLittleEndian(data, 1, width); // submesh count
        AppendLittleEndian(data, k_index_count, width);
        AppendLittleEndian(data, k_vertex_count, width);
        AppendLittleEndian(data, k_vertex_buffer_bytes, width);
        for (const AttributeSpec &attribute : attributes) {
            // The enum, padded to the width of a `size_t`, then the two sizes at that width.
            AppendLittleEndian(data, attribute.type, width);
            AppendLittleEndian(data, attribute.buffer_offset, width);
            AppendLittleEndian(data, attribute.buffer_size, width);
        }
        for (uint32_t index = 0; index < k_index_count; index++) {
            AppendLittleEndian(data, index, sizeof(uint32_t));
        }
        data.resize(data.size() + k_vertex_buffer_bytes);
        return data;
    }

    /// @brief Read a legacy archive written by a `size_t` of the given width and check the semantics.
    void CheckLegacyOrder(size_t width, const std::string &label) {
        MeshAsset mesh;
        const auto data = MakeLegacyMesh(width);
        Check(mesh.LoadLegacyMesh(data), label + ": the legacy archive loads");

        if (mesh.m_submeshes.size() != 1) {
            Check(false, label + ": exactly one submesh is read");
            return;
        }
        const MeshAsset::Submesh &submesh = mesh.m_submeshes[0];
        Check(submesh.m_indices.size() == k_index_count, label + ": the index buffer is read");
        Check(submesh.vertex_count == k_vertex_count, label + ": the vertex count is read");
        Check(submesh.m_vertex_attributes.size() == k_vertex_buffer_bytes, label + ": the vertex buffer is read");

        // The defect this test exists for: reading the slots in the fixed-width order turns the
        // UV into a `vec2` tangent and the tangent into a `vec4` UV, and the lit pass then builds
        // its TBN from the UV stream.
        Check(
            submesh.texcoord0.type == VertexAttributeType::SFloat32x2,
            label + ": texcoord0 is read as the vec2 stream"
        );
        Check(submesh.texcoord0.buffer_offset == k_texcoord_offset, label + ": texcoord0 keeps its offset");
        Check(submesh.tangent.type == VertexAttributeType::SFloat32x4, label + ": tangent is read as the vec4 stream");
        Check(submesh.tangent.buffer_offset == k_tangent_offset, label + ": tangent keeps its offset");
        Check(submesh.normal.type == VertexAttributeType::SFloat32x3, label + ": normal is untouched");
        Check(submesh.color.type == VertexAttributeType::Unused, label + ": an unused slot stays unused");
    }
} // namespace

int main() {
    CheckLegacyOrder(8, "legacy 64-bit writer");
    CheckLegacyOrder(4, "legacy 32-bit writer");

    if (failures != 0) {
        std::cerr << failures << " legacy mesh check(s) failed.\n";
        return 1;
    }
    std::cout << "All legacy mesh checks passed.\n";
    return 0;
}
