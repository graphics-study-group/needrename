#ifndef PIPELINE_COMMANDBUFFER_GraphicsCommandBuffer_INCLUDED
#define PIPELINE_COMMANDBUFFER_GraphicsCommandBuffer_INCLUDED

#include "Render/VkWrapper.tcc"
#include "Render/AttachmentUtils.h"
#include "Render/Pipeline/CommandBuffer/ICommandBuffer.h"
#include <vulkan/vulkan.hpp>
#include <glm.hpp>

namespace Engine {
    class Material;
    class MaterialInstance;
    class HomogeneousMesh;
    class Buffer;
    class RenderTargetBinding;

    /// @brief A command buffer used for rendering.
    class GraphicsCommandBuffer : public ICommandBuffer
    {
    public:
        GraphicsCommandBuffer (
            RenderSystem & system,
            vk::CommandBuffer cb,
            uint32_t frame_in_flight
        );

        GraphicsCommandBuffer (const GraphicsCommandBuffer &) = delete;
        GraphicsCommandBuffer (GraphicsCommandBuffer &&) = default;
        GraphicsCommandBuffer & operator = (const GraphicsCommandBuffer &) = delete;
        GraphicsCommandBuffer & operator = (GraphicsCommandBuffer &&) = default;

        /// @brief Begin a Vulkan rendering pass
        void BeginRendering(
            AttachmentUtils::AttachmentDescription color, 
            AttachmentUtils::AttachmentDescription depth, 
            vk::Extent2D extent,
            const std::string & name = ""
        );

        void BeginRendering(
            const RenderTargetBinding & binding,
            vk::Extent2D extent,
            const std::string & name = ""
        );

        /// @brief Bind a material for rendering, and write per-material descriptors.
        /// @param material 
        /// @param pass_index 
        void BindMaterial(MaterialInstance & material, uint32_t pass_index);

        /// @brief Setup the viewport parameters
        /// @param vpWidth width of the viewport
        /// @param vpHeight height of the viewport
        /// @param scissor scissor rectangle
        void SetupViewport(float vpWidth, float vpHeight, vk::Rect2D scissor);

        /// @brief Write per-mesh descriptors, and send draw call to GPU.
        /// @param mesh 
        void DrawMesh(const HomogeneousMesh & mesh);
        void DrawMesh(const HomogeneousMesh& mesh, const glm::mat4 & model_matrix);

        /// @brief End the render pass
        void EndRendering();

        void Reset() noexcept override;

    protected:
        RenderSystem & m_system;

        uint32_t m_inflight_frame_index ;

        std::optional<std::pair<vk::Pipeline, vk::PipelineLayout>> m_bound_material_pipeline {};
    };
}

#endif // PIPELINE_COMMANDBUFFER_GraphicsCommandBuffer_INCLUDED
