#ifndef PIPELINE_COMMANDBUFFER_RENDERCOMMANDBUFFER_INCLUDED
#define PIPELINE_COMMANDBUFFER_RENDERCOMMANDBUFFER_INCLUDED

#include "Render/VkWrapper.tcc"
#include "Render/AttachmentUtils.h"
#include "Render/Pipeline/CommandBuffer/LayoutTransferHelper.h"
#include "Render/Pipeline/CommandBuffer/ICommandBuffer.h"
#include <vulkan/vulkan.hpp>
#include <glm.hpp>

namespace Engine {
    class Material;
    class MaterialInstance;
    class HomogeneousMesh;
    class Buffer;
    class AllocatedImage2DTexture;
    class RenderTargetBinding;

    /// @brief A command buffer used for rendering.
    class RenderCommandBuffer : public ICommandBuffer
    {
    public:
        using AttachmentBarrierType = LayoutTransferHelper::AttachmentBarrierType;

        RenderCommandBuffer (
            RenderSystem & system,
            vk::CommandBuffer cb,
            uint32_t frame_in_flight
        );

        RenderCommandBuffer (const RenderCommandBuffer &) = delete;
        RenderCommandBuffer (RenderCommandBuffer &&) = default;
        RenderCommandBuffer & operator = (const RenderCommandBuffer &) = delete;
        RenderCommandBuffer & operator = (RenderCommandBuffer &&) = default;

        /// @brief Begin a Vulkan rendering pass
        void BeginRendering(
            AttachmentUtils::AttachmentDescription color, 
            AttachmentUtils::AttachmentDescription depth, 
            vk::Extent2D extent
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

        void Reset();

    protected:
        RenderSystem & m_system;

        uint32_t m_inflight_frame_index ;

        std::optional<std::pair<vk::Pipeline, vk::PipelineLayout>> m_bound_material_pipeline {};
    };
}

#endif // PIPELINE_COMMANDBUFFER_RENDERCOMMANDBUFFER_INCLUDED
