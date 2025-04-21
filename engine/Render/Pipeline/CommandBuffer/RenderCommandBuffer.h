#ifndef PIPELINE_COMMANDBUFFER_RENDERCOMMANDBUFFER_INCLUDED
#define PIPELINE_COMMANDBUFFER_RENDERCOMMANDBUFFER_INCLUDED

#include "Render/VkWrapper.tcc"
#include "Render/AttachmentUtils.h"
#include "Render/Pipeline/CommandBuffer/LayoutTransferHelper.h"
#include <vulkan/vulkan.hpp>
#include <glm.hpp>

namespace Engine {
    class Material;
    class MaterialInstance;
    class HomogeneousMesh;
    class Buffer;
    class AllocatedImage2DTexture;

    /// @brief A command buffer used for rendering.
    class RenderCommandBuffer
    {
    public:
        using AttachmentBarrierType = LayoutTransferHelper::AttachmentBarrierType;

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
        void BeginRendering(
            AttachmentUtils::AttachmentDescription color, 
            AttachmentUtils::AttachmentDescription depth, 
            vk::Extent2D extent, 
            uint32_t framebuffer_id
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

        /**
         * @brief Insert a barrier for an given image used as color or depth attachment.
         * Inserted barrier will establish a memory dependency between correpsonding stages to avoid the given hazard.
         * Further, the image layout will be transfered to be adequate for attachment write or shader read.
         */
        void InsertAttachmentBarrier(AttachmentBarrierType type, vk::Image image);

        /// @brief Write per-mesh descriptors, and send draw call to GPU.
        /// @param mesh 
        void DrawMesh(const HomogeneousMesh & mesh);
        void DrawMesh(const HomogeneousMesh& mesh, const glm::mat4 & model_matrix);

        /// @brief End the render pass
        void EndRendering();

        /// @brief End recording of the command buffer
        void End();

        /// @brief Submit the command buffer to graphics queue
        /// @param wait whether wait for the semaphore before execution. Used for the first CB only.
        void Submit(bool wait_for_semaphore = true);

        void Reset();

        vk::CommandBuffer get();
    protected:
        RenderSystem & m_system;

        uint32_t m_inflight_frame_index ;
        vk::CommandBuffer m_handle;
        vk::Queue m_queue;
        vk::Fence m_completed_fence;
        vk::Semaphore m_wait_semaphore, m_signal_semaphore;

        std::optional<std::pair<vk::Pipeline, vk::PipelineLayout>> m_bound_material_pipeline {};
    };
}

#endif // PIPELINE_COMMANDBUFFER_RENDERCOMMANDBUFFER_INCLUDED
