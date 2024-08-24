#ifndef RENDER_RENDERER_MESH_INCLUDED
#define RENDER_RENDERER_MESH_INCLUDED

#include "Render/RenderSystem.h"

namespace Engine{
    
    /// @brief A homogeneous mesh of only one material at runtime, constructed from mesh asset.
    class HomogeneousMesh {
        static constexpr const uint32_t BINDING_COUNT = 2;
        static constexpr const size_t SINGLE_VERTEX_BUFFER_SIZE = sizeof(float) * 6;
    public:
        HomogeneousMesh(std::weak_ptr <RenderSystem> system);
        ~HomogeneousMesh();

        void Prepare();

        static vk::PipelineVertexInputStateCreateInfo GetVertexInputState();

        uint32_t GetVertexCount() const;

        std::pair <std::array<vk::Buffer, HomogeneousMesh::BINDING_COUNT>, std::array<vk::DeviceSize, HomogeneousMesh::BINDING_COUNT>>
        GetBindingInfo() const;

    protected:
        std::weak_ptr <RenderSystem> m_system;

        static constexpr const std::array<vk::VertexInputBindingDescription, BINDING_COUNT> bindings = {
            // Position
            vk::VertexInputBindingDescription{0, sizeof(float)*3, vk::VertexInputRate::eVertex},
            // Vertex color
            vk::VertexInputBindingDescription{1, sizeof(float)*3, vk::VertexInputRate::eVertex}
        };

        static constexpr const std::array<vk::VertexInputAttributeDescription, BINDING_COUNT> attributes = {
            // Position
            vk::VertexInputAttributeDescription{0, bindings[0].binding, vk::Format::eR32G32B32Sfloat, 0},
            // Vertex color
            vk::VertexInputAttributeDescription{1, bindings[1].binding, vk::Format::eR32G32B32Sfloat, 0},
        };

        vk::UniqueBuffer m_buffer {};
        vk::UniqueDeviceMemory m_memory {};

        uint32_t m_vertex_count {0};

        std::vector <float> m_positions {};
        std::vector <float> m_colors {};
        std::vector <float> m_normals {};
        std::vector <float> m_uvs {};
    };
};

#endif // RENDER_RENDERER_MESH_INCLUDED
