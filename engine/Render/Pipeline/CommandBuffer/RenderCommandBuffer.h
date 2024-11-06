#ifndef PIPELINE_COMMANDBUFFER_RENDERCOMMANDBUFFER_INCLUDED
#define PIPELINE_COMMANDBUFFER_RENDERCOMMANDBUFFER_INCLUDED

#include "Render/VkWrapper.tcc"
#include <vulkan/vulkan.hpp>
#include <glm.hpp>

namespace Engine {
    class RenderTargetSetup;
    class Material;
    class Synchronization;
    class HomogeneousMesh;
    class Buffer;
    class AllocatedImage2DTexture;

    /// @brief A command buffer used for rendering.
    class RenderCommandBuffer
    {
    public:
        /// @brief Create a command buffer used for rendering.
        /// This function is ideally only called from RenderSystem
        /// @param logical_device 
        /// @param command_pool 
        void CreateCommandBuffer(
            std::shared_ptr<RenderSystem> system, 
            vk::CommandPool command_pool, 
            vk::Queue queue, 
            uint32_t inflight_frame_index
        );

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
        void BindMaterial(Material & material, uint32_t pass_index, bool skinned = false);

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
        uint32_t m_inflight_frame_index {};
        vk::UniqueCommandBuffer m_handle {};
        vk::Queue m_queue {};

        RenderSystem * m_system {nullptr};

        std::optional<vk::Image> m_image_for_present {};
        std::optional<std::reference_wrapper<const RenderTargetSetup>> m_bound_render_target {};
        std::optional<std::pair <std::reference_wrapper<const Material>, uint32_t>> m_bound_material {};
        std::optional<std::pair<vk::Pipeline, vk::PipelineLayout>> m_bound_material_pipeline {};
    };
}

#endif // PIPELINE_COMMANDBUFFER_RENDERCOMMANDBUFFER_INCLUDED
