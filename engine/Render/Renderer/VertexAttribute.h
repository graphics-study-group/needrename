#ifndef RENDER_RENDERER_VERTEXATTRIBUTE_INCLUDED
#define RENDER_RENDERER_VERTEXATTRIBUTE_INCLUDED

#include <cstdint>
#include <vector>

namespace vk {
    enum class Format;

    class VertexInputBindingDescription;
    class VertexInputAttributeDescription;
}

namespace Engine {
    namespace RenderSystemState {
        class DeviceInterface;
        class AllocatorState;
    }
    /**
     * @brief Type of the vertex attribute.
     * Up to 16 types (incl. unused) are supported.
     * Currently there are only 8 types.
     */
    enum class VertexAttributeType : uint8_t {
        Unused,
        SFloat32x1,
        SFloat32x2,
        SFloat32x3,
        SFloat32x4,
        Uint8x4,
        Uint16x2,
        Uint32x1,
        Reserved0,
        Reserved1,
        Reserved2,
        Reserved3,
        Reserved4,
        Reserved5,
        Reserved6,
        Reserved7
    };

    /**
     * @brief Semantic of the vertex attribute.
     * Up to 16 semantics are supported.
     */
    enum class VertexAttributeSemantic : uint8_t {
        Position,
        Color,
        Normal,
        Tangent,
        Texcoord0,
        Texcoord1,
        Texcoord2,
        Texcoord3,
        BoneIndices,
        BoneWeights,
        Extra0,
        Extra1,
        Extra2,
        Extra3,
        Extra4,
        Extra5
    };

    /**
     * @brief A struct that stores vertex attribute contained in a mesh.
     * Types of vertex attributes are packed and stored in order as a
     * bitset, with each contingous 8 bits representing a semantic.
     */
    struct VertexAttribute {
        uint64_t packed;

        VertexAttribute & SetAttribute(VertexAttributeSemantic semantic, VertexAttributeType type) noexcept {
            uint64_t type_bits {static_cast<uint8_t>(type)};
            uint64_t mask {0xF};
            mask <<= static_cast<uint8_t>(semantic) * 4;
            type_bits <<= static_cast<uint8_t>(semantic) * 4;
            packed = ((packed & ~mask) | type_bits);
            return *this;
        }

        VertexAttributeType GetAttribute(VertexAttributeSemantic semantic) const noexcept {
            auto shifted = packed >> static_cast<uint8_t>(semantic) * 4;
            return static_cast<VertexAttributeType>(shifted & 0xF);
        }

        bool HasAttribute(VertexAttributeSemantic semantic) const noexcept {
            return GetAttribute(semantic) != VertexAttributeType::Unused;
        }

        /**
         * @brief Generate a vertex input binding description from the
         * stored vertex attribute.
         * Each vertex attibute corresponds to exactly one binding, and
         * the stride of the binding is exactly the same as the size
         * occupied by one item of the attribute (i.e. 128 for `SFloat32x4`).
         * The input rate is set to per vertex.
         * The binding numbers are guranteed to be contingous and increasing.
         */
        std::vector <vk::VertexInputBindingDescription> ToVkVertexInputBinding() const noexcept;

        /**
         * @brief Generate a vertex input attribute description from the
         * store vertex attribute.
         * The location of each attribute will be identical to its underlying
         * enum integer (i.e 1 for `Normal`), and its bindings will be contingous
         * and increasing, corresponding to the ones returned by 
         * `ToVkVertexInputBinding`.
         * Offsets are set to zero, and true offsets of the attributes into the 
         * underlying buffer will be determined at bind time.
         *
         * @param allocator a pointer to the allocator to check format limits.
         * Pass null to ignore the checks.
         */
        std::vector <vk::VertexInputAttributeDescription> ToVkVertexAttribute(
            RenderSystemState::AllocatorState * allocator = nullptr
        ) const noexcept;

        /**
         * @brief Get total vertex size.
         */
        uint32_t GetTotalPerVertexSize() const noexcept;

        /**
         * @brief Get a per vertex size, enough to hold all attributes.
         * @note This size does not include the size of the index buffer.
         */
        uint32_t GetPerVertexSize(VertexAttributeSemantic semantic) const noexcept;

        /**
         * @brief Get the multiplier of the offset of the semantic.
         * The offset of the given semantic is caculated by multiply
         * the factor and the vertex count.
         */
        uint64_t GetOffsetFactor(VertexAttributeSemantic semantic) const noexcept;

        /**
         * @brief Enumerate the offset factor of all used attributes.
         */
        std::vector <uint64_t> EnumerateOffsetFactor() const noexcept;

        static constexpr VertexAttribute GetDefaultBasicVertexAttribute() {
            return VertexAttribute{
                .packed =
                    // Position
                    (static_cast<uint8_t>(VertexAttributeType::SFloat32x3)) |
                    // Color
                    (static_cast<uint8_t>(VertexAttributeType::SFloat32x3) << 4) |
                    // Normal
                    (static_cast<uint8_t>(VertexAttributeType::SFloat32x3) << 8) |
                    // Texcoord0
                    (static_cast<uint8_t>(VertexAttributeType::SFloat32x2) << 12)
            };
        }
    };
}

#endif // RENDER_RENDERER_VERTEXATTRIBUTE_INCLUDED
