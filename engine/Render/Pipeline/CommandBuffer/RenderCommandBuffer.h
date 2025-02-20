#ifndef PIPELINE_COMMANDBUFFER_RENDERCOMMANDBUFFER_INCLUDED
#define PIPELINE_COMMANDBUFFER_RENDERCOMMANDBUFFER_INCLUDED

#include "Render/VkWrapper.tcc"
#include <vulkan/vulkan.hpp>
#include <glm.hpp>

namespace Engine {
    class RenderTargetSetup;
    class Material;
    class MaterialInstance;
    class HomogeneousMesh;
    class Buffer;
    class AllocatedImage2DTexture;

    /// @brief A command buffer used for rendering.
    class RenderCommandBuffer
    {
    public:
        RenderCommandBuffer (
            RenderSystem & system,
            vk::CommandBuffer cb,
            vk::Queue queue,
            vk::Fence fence,
            vk::Semaphore wait,
            vk::Semaphore signal,
            uint32_t frame_in_flight
        );

        RenderCommandBuffer (const RenderCommandBuffer &) = delete;
        RenderCommandBuffer (RenderCommandBuffer &&) = default;
        RenderCommandBuffer & operator = (const RenderCommandBuffer &) = delete;
        RenderCommandBuffer & operator = (RenderCommandBuffer &&) = default;

        /// @brief Record a begin command in command buffer
        void Begin();

        /// @brief Begin a Vulkan rendering pass
        /// @param pass render targets
        /// @param extent extent of the rendering pass
        /// @param framebuffer_id ID of the framebuffer, acquired from GetNextImage.
        void BeginRendering(const RenderTargetSetup & pass, vk::Extent2D extent, uint32_t framebuffer_id);

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

        /// @brief End recording of the command buffer
        void End();

        /// @brief Submit the command buffer to graphics queue
        void Submit();

        void Reset();

        vk::CommandBuffer get();
    protected:
        RenderSystem & m_system;

        uint32_t m_inflight_frame_index ;
        vk::CommandBuffer m_handle;
        vk::Queue m_queue;
        vk::Fence m_completed_fence;
        vk::Semaphore m_image_ready_semaphore, m_completed_semaphore;

        std::optional<vk::Image> m_image_for_present {};
        std::optional<std::reference_wrapper<const RenderTargetSetup>> m_bound_render_target {};
        std::optional<std::pair<vk::Pipeline, vk::PipelineLayout>> m_bound_material_pipeline {};
    };
}

#endif // PIPELINE_COMMANDBUFFER_RENDERCOMMANDBUFFER_INCLUDED
