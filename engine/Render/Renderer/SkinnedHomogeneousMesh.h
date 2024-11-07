#ifndef RENDER_RENDERER_SKINNEDHOMOGENEOUSMESH_INCLUDED
#define RENDER_RENDERER_SKINNEDHOMOGENEOUSMESH_INCLUDED

#include "HomogeneousMesh.h"

namespace Engine {
    class SkinnedHomogeneousMesh : public HomogeneousMesh {
    public:
        static constexpr const uint32_t BINDING_COUNT = 3;

        SkinnedHomogeneousMesh(std::weak_ptr <RenderSystem> system);
        virtual ~SkinnedHomogeneousMesh() = default;

        virtual uint64_t GetExpectedBufferSize() const override;

        virtual 
        std::pair <std::vector<vk::Buffer>, std::vector<vk::DeviceSize>>
        GetBindingInfo() const override;

        virtual
        std::pair <vk::Buffer, vk::DeviceSize>
        GetIndexInfo() const;

        static
        vk::PipelineVertexInputStateCreateInfo
        GetVertexInputState();

        void SetBones (std::vector <VertexStruct::SkinnedVertexAttribute> attributes);

    protected:
        static constexpr const std::array<vk::VertexInputBindingDescription, BINDING_COUNT> BINDINGS = {
            // Position
            vk::VertexInputBindingDescription{0, VertexStruct::VERTEX_POSITION_SIZE, vk::VertexInputRate::eVertex},
            // Other attributes
            vk::VertexInputBindingDescription{1, VertexStruct::VERTEX_ATTRIBUTE_SIZE, vk::VertexInputRate::eVertex},
            // Bone indices and weights
            vk::VertexInputBindingDescription{2, VertexStruct::SKINNED_VERTEX_ATTRIBUTE_SIZE, vk::VertexInputRate::eVertex}
        };

        static constexpr const 
        std::array<vk::VertexInputAttributeDescription, VertexStruct::SKINNED_VERTEX_ATTRIBUTE_COUNT + VertexStruct::VERTEX_ATTRIBUTE_COUNT + 1> 
        ATTRIBUTES = {
            // Position
            HomogeneousMesh::ATTRIBUTES[0],
            // Vertex color
            HomogeneousMesh::ATTRIBUTES[1],
            // Vertex normal
            HomogeneousMesh::ATTRIBUTES[2],
            // Texcoord 1
            HomogeneousMesh::ATTRIBUTES[3],
            // Bone indices
            vk::VertexInputAttributeDescription{
                4,
                BINDINGS[2].binding,
                vk::Format::eR32G32B32A32Uint,
                VertexStruct::OFFSET_BONE_INDEX
            },
            // Bone weights
            vk::VertexInputAttributeDescription{
                5,
                BINDINGS[2].binding,
                vk::Format::eR32G32B32A32Sfloat,
                VertexStruct::OFFSET_BONE_INDEX
            }
        };

        std::vector <VertexStruct::SkinnedVertexAttribute> m_bones {};
        virtual void WriteToMemory(std::byte * pointer) const;
    };
}

#endif // RENDER_RENDERER_SKINNEDHOMOGENEOUSMESH_INCLUDED
