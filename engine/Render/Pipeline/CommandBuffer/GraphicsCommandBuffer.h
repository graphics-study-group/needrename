#ifndef PIPELINE_COMMANDBUFFER_GRAPHICSCOMMANDBUFFER_INCLUDED
#define PIPELINE_COMMANDBUFFER_GRAPHICSCOMMANDBUFFER_INCLUDED


#include "Render/RenderSystem/RendererManager.h"
#include "Render/Pipeline/CommandBuffer/TransferCommandBuffer.h"

// GLM forward declaration.
#include <fwd.hpp>

namespace vk {
    class CommandBuffer;
    class Pipeline;
    class PipelineLayout;
    class Extent2D;
}

namespace Engine {
    class Material;
    class MaterialInstance;
    class HomogeneousMesh;
    class Buffer;
    class RenderTargetBinding;

    namespace AttachmentUtils {
        class AttachmentDescription;
    };

    /**
     * @brief A command buffer used for rendering.
     * 
     * `GraphicsCommandBuffer` inherits `TranferCommandBuffer` to facilitate memory
     * operations like blitting and clearing. However these operations are generally
     * only allowed outside a rendering pass. You need to call `EndRendering()` and
     * setup proper barriers with the context before recording these commands.
     */
    class GraphicsCommandBuffer : public TransferCommandBuffer
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
            const AttachmentUtils::AttachmentDescription & color, 
            const AttachmentUtils::AttachmentDescription & depth, 
            vk::Extent2D extent,
            const std::string & name = ""
        );

        void BeginRendering(
            const RenderTargetBinding & binding,
            vk::Extent2D extent,
            const std::string & name = ""
        );

        /**
         * @brief Bind a material for rendering.
         * 
         * Bind new material pipeline to the GPU (if warranted), bind descriptors to the pipeline,
         * and write pending uniform data updates of the given material instance.
         * 
         * @note Camera data is uploaded to the pipeline in this method call. If camera switch occurred,
         * this method must be called again even if material is the same. This case should be handled
         * by the render system.
         */
        void BindMaterial(MaterialInstance & material, uint32_t pass_index);

        /// @brief Setup the viewport parameters
        /// @param vpWidth width of the viewport
        /// @param vpHeight height of the viewport
        /// @param scissor scissor rectangle
        void SetupViewport(float vpWidth, float vpHeight, vk::Rect2D scissor);

        /// @brief Write per-mesh descriptors, and send draw call to GPU.
        /// @param mesh 
        void DrawMesh(const HomogeneousMesh & mesh);
        void DrawMesh(const HomogeneousMesh & mesh, const glm::mat4 & model_matrix);

        void DrawRenderers (const RendererList & renderers, uint32_t pass);
        void DrawRenderers (
            const RendererList & renderers, 
            const glm::mat4 &view_matrix, 
            const glm::mat4 &projection_matrix, 
            vk::Extent2D extent, 
            uint32_t pass
        );

        /// @brief End the render pass
        void EndRendering();

        void Reset() noexcept override;

    protected:
        uint32_t m_inflight_frame_index ;
        std::optional<std::pair<vk::Pipeline, vk::PipelineLayout>> m_bound_material_pipeline {};
    };
}

#endif // PIPELINE_COMMANDBUFFER_GRAPHICSCOMMANDBUFFER_INCLUDED
