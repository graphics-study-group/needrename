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
        Normal,
        Tangent,
        Color,
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

        /**
         * @brief Generate a vertex input binding description from the
         * stored vertex attribute.
         * Each vertex attibute corresponds to exactly one binding, and
         * the stride of the binding is exactly the same as the size
         * occupied by one item of the attribute (i.e. 128 for `SFloat32x4`).
         * The input rate is set to per mesh.
         * The binding numbers are guranteed to be contingous and increasing.
         *
         * @param device a pointer to the device interface to check physical limits.
         * Pass null to ignore the checks.
         */
        std::vector <vk::VertexInputBindingDescription> ToVkVertexInputBinding(
            RenderSystemState::DeviceInterface * device = nullptr
        ) const noexcept;

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
         * @param device a pointer to the device interface to check physical limits.
         * Pass null to ignore the checks.
         * @param allocator a pointer to the allocator to check format limits.
         * Pass null to ignore the checks.
         */
        std::vector <vk::VertexInputAttributeDescription> ToVkVertexAttribute(
            RenderSystemState::DeviceInterface * device = nullptr,
            RenderSystemState::AllocatorState * allocator = nullptr
        ) const noexcept;
    };
}

#endif // RENDER_RENDERER_VERTEXATTRIBUTE_INCLUDED
