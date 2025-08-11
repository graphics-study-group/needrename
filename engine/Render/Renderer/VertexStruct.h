#ifndef RENDER_RENDERER_VERTEXSTRUCT_INCLUDED
#define RENDER_RENDERER_VERTEXSTRUCT_INCLUDED

#include <cctype>
#include <vulkan/vulkan.hpp>

namespace Engine {
    namespace VertexStruct {
        struct VertexPosition {
            // Object space position vector.
            float position[3];
        };

        struct VertexAttributeBasic {
            // Vertex color in linear space.
            float color[3];
            // Object space normal vector.
            float normal[3];
            // Texture coordinate (UV) No. 1
            float texcoord1[2];
        };

        struct VertexAttributeExtended {
            // Object space tangent vector.
            // W component is used to determine bi-tangent:
            // `bitangent = cross(normal, tangent.xyz) * tangent.w;`
            float tangent[4];
            // Texture coordinate (UV) No. 2
            float texcoord2[2];
            // Texture coordinate (UV) No. 3
            float texcoord3[2];
            // Texture coordinate (UV) No. 4
            float texcoord4[2];
        };

        struct VertexAttributeSkinned {
            uint16_t bone_id[4];
            float weight[4];
        };

        constexpr uint32_t OFFSET_COLOR = offsetof(VertexAttributeBasic, color);
        constexpr uint32_t OFFSET_NORMAL = offsetof(VertexAttributeBasic, normal);
        constexpr uint32_t OFFSET_TEXCOORD1 = offsetof(VertexAttributeBasic, texcoord1);

        constexpr uint32_t VERTEX_ATTRIBUTE_BASIC_COUNT = 3;
        constexpr uint32_t VERTEX_ATTRIBUTE_EXTENDED_COUNT = 4;
        constexpr uint32_t VERTEX_ATTRIBUTE_SKINNED_COUNT = 2;

        constexpr const std::array<size_t, 4> PER_VERTEX_SIZE_DELTA = {
            sizeof(VertexPosition),
            sizeof(VertexAttributeBasic),
            sizeof(VertexAttributeExtended),
            sizeof(VertexAttributeSkinned)
        };

        constexpr const std::array<size_t, 4> PER_VERTEX_SIZE = {
            sizeof(VertexPosition),
            sizeof(VertexPosition) + sizeof(VertexAttributeBasic),
            sizeof(VertexPosition) + sizeof(VertexAttributeBasic) + sizeof(VertexAttributeExtended),
            sizeof(VertexPosition) + sizeof(VertexAttributeBasic) + sizeof(VertexAttributeExtended)
                + sizeof(VertexAttributeSkinned)
        };

        constexpr const std::array<vk::VertexInputBindingDescription, 2> BINDINGS_BASIC = {
            // Position
            vk::VertexInputBindingDescription{0, sizeof(VertexPosition), vk::VertexInputRate::eVertex},
            // Other attributes
            vk::VertexInputBindingDescription{1, sizeof(VertexAttributeBasic), vk::VertexInputRate::eVertex}
        };
        constexpr const std::array<vk::VertexInputAttributeDescription, VertexStruct::VERTEX_ATTRIBUTE_BASIC_COUNT + 1>
            ATTRIBUTES_BASIC = {
                // Position
                vk::VertexInputAttributeDescription{0, BINDINGS_BASIC[0].binding, vk::Format::eR32G32B32Sfloat, 0},
                // Vertex color
                vk::VertexInputAttributeDescription{
                    1, BINDINGS_BASIC[1].binding, vk::Format::eR32G32B32Sfloat, VertexStruct::OFFSET_COLOR
                },
                // Vertex normal
                vk::VertexInputAttributeDescription{
                    2, BINDINGS_BASIC[1].binding, vk::Format::eR32G32B32Sfloat, VertexStruct::OFFSET_NORMAL
                },
                // Texcoord 1
                vk::VertexInputAttributeDescription{
                    3, BINDINGS_BASIC[1].binding, vk::Format::eR32G32Sfloat, VertexStruct::OFFSET_TEXCOORD1
                }
        };

    }; // namespace VertexStruct
}; // namespace Engine

#endif // RENDER_RENDERER_VERTEXSTRUCT_INCLUDED
