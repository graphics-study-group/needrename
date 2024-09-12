#ifndef RENDER_RENDERER_HOMOGENEOUSMESH_INCLUDED
#define RENDER_RENDERER_HOMOGENEOUSMESH_INCLUDED

#include "Render/Renderer/VertexStruct.h"
#include "Render/RenderSystem.h"
#include "Render/Memory/Buffer.h"

namespace Engine{
    
    /// @brief A homogeneous mesh of only one material at runtime, constructed from mesh asset.
    class HomogeneousMesh {
    public:

        static constexpr const uint32_t BINDING_COUNT = 2;

        HomogeneousMesh(std::weak_ptr <RenderSystem> system);
        ~HomogeneousMesh();

        void Prepare();

        /// @brief Check if the vertex buffer of the mesh needs to be commited to GPU.
        /// The flag is always set to `false` after this check.
        /// @return whether commitment is needed.
        bool NeedCommitment();

        Buffer CreateStagingBuffer() const;
        uint32_t GetVertexIndexCount() const;
        uint32_t GetVertexCount() const;
        uint64_t GetExpectedBufferSize() const;
        const Buffer & GetBuffer() const;

        std::pair <
            std::array<vk::Buffer, HomogeneousMesh::BINDING_COUNT>, 
            std::array<vk::DeviceSize, HomogeneousMesh::BINDING_COUNT>
        >
        GetBindingInfo() const;

        std::pair <vk::Buffer, vk::DeviceSize>
        GetIndexInfo() const;

        static vk::PipelineVertexInputStateCreateInfo GetVertexInputState();

        void SetPositions (std::vector <VertexStruct::VertexPosition> positions);
        void SetAttributes (std::vector <VertexStruct::VertexAttribute> attributes);
        void SetIndices (std::vector <uint32_t> indices);

        void SetModelTransform(glm::mat4 matrix);
        const glm::mat4 & GetModelTransform() const;

    protected:
        std::weak_ptr <RenderSystem> m_system;

        static constexpr const std::array<vk::VertexInputBindingDescription, BINDING_COUNT> bindings = {
            // Position
            vk::VertexInputBindingDescription{0, VertexStruct::VERTEX_POSITION_SIZE, vk::VertexInputRate::eVertex},
            // Other attributes
            vk::VertexInputBindingDescription{1, VertexStruct::VERTEX_ATTRIBUTE_SIZE, vk::VertexInputRate::eVertex}
        };

        static constexpr const std::array<vk::VertexInputAttributeDescription, VertexStruct::VERTEX_ATTRIBUTE_COUNT + 1> attributes = {
            // Position
            vk::VertexInputAttributeDescription{0, bindings[0].binding, vk::Format::eR32G32B32Sfloat, 0},
            // Vertex color
            vk::VertexInputAttributeDescription{
                1, 
                bindings[1].binding, 
                vk::Format::eR32G32B32Sfloat, 
                VertexStruct::OFFSET_COLOR
            },
            // Vertex normal
            vk::VertexInputAttributeDescription{
                2, 
                bindings[1].binding, 
                vk::Format::eR32G32B32Sfloat, 
                VertexStruct::OFFSET_NORMAL
            },
            // Texcoord 1
            vk::VertexInputAttributeDescription{
                3,
                bindings[1].binding,
                vk::Format::eR32G32Sfloat,
                VertexStruct::OFFSET_TEXCOORD1
            }
        };

        Buffer m_buffer;

        bool m_updated {false};

        uint64_t m_allocated_buffer_size {0};

        std::vector <uint32_t> m_indices {};
        std::vector <VertexStruct::VertexPosition> m_positions {};
        std::vector <VertexStruct::VertexAttribute> m_attributes {};

        glm::mat4 m_model_transform {1.0f};

        void WriteToMemory(std::byte * pointer) const;
    };
};

#endif // RENDER_RENDERER_HOMOGENEOUSMESH_INCLUDED
