#ifndef PIPELINE_COMMANDBUFFER_GRAPHICSCOMMANDBUFFER_INCLUDED
#define PIPELINE_COMMANDBUFFER_GRAPHICSCOMMANDBUFFER_INCLUDED

#include "Render/Pipeline/CommandBuffer/TransferCommandBuffer.h"
#include "Render/Renderer/HomogeneousMesh.h"
#include "Render/RenderSystem/RendererManager.h"

// GLM forward declaration.
#include <fwd.hpp>

namespace vk {
    class CommandBuffer;
    class Pipeline;
    class PipelineLayout;
    class Extent2D;
} // namespace vk

namespace Engine {
    class MaterialTemplate;
    class MaterialInstance;
    class Buffer;

    namespace AttachmentUtils {
        class AttachmentDescription;
    };

    /**
     * @brief A command buffer used for rendering.
     * 
     * `GraphicsCommandBuffer` inherits
     * `TranferCommandBuffer` to facilitate memory
     * operations like blitting and clearing. However these
     * operations are generally
     * only allowed outside a rendering pass. You need to call `EndRendering()` and

     * * setup proper barriers with the context before recording these commands.
     */
    class GraphicsCommandBuffer : public TransferCommandBuffer {
    public:
        GraphicsCommandBuffer(RenderSystem &system, vk::CommandBuffer cb, uint32_t frame_in_flight);

        GraphicsCommandBuffer(const GraphicsCommandBuffer &) = delete;
        GraphicsCommandBuffer(GraphicsCommandBuffer &&) = default;
        GraphicsCommandBuffer &operator=(const GraphicsCommandBuffer &) = delete;
        GraphicsCommandBuffer &operator=(GraphicsCommandBuffer &&) = default;

        /// @brief Begin a Vulkan rendering pass
        void BeginRendering(
            const AttachmentUtils::AttachmentDescription &color,
            const AttachmentUtils::AttachmentDescription &depth,
            vk::Extent2D extent,
            const std::string &name = ""
        );

        /**
         * @brief Begin a Vulkan rendering pass with Multiple Render Targets (MRT)
         */
        void BeginRendering(
            const std::vector <AttachmentUtils::AttachmentDescription> & colors,
            const AttachmentUtils::AttachmentDescription depth,
            vk::Extent2D extent,
            const std::string & name = ""
        );

        /**
         * @brief Bind a material for rendering.
         * 
         * Bind new material pipeline to the
         * GPU (if warranted), bind descriptors to the pipeline,
         * and write pending uniform data updates of
         * the given material instance.
         * 
         * May perform lazy allocation of buffers, etc.
         */
        void BindMaterial(
            MaterialInstance &inst,
            const std::string & tag,
            HomogeneousMesh::MeshVertexType type
        );

        /// @brief Setup the viewport parameters
        /// @param vpWidth width of the viewport
        /// @param vpHeight height of the viewport
        /// @param scissor scissor rectangle
        void SetupViewport(float vpWidth, float vpHeight, vk::Rect2D scissor);

        /**
         * @brief Minimalistic interface for drawing a mesh.
         * 
         * Write per-mesh data, and send draw call to GPU.
         * Does not do any extra stuff such as setting up viewports.
         */
        void DrawMesh(const HomogeneousMesh &mesh);
        void DrawMesh(const HomogeneousMesh &mesh, const glm::mat4 &model_matrix);

        /**
         * @brief Draw renderers in the RendererList with specified pass index.
         * 
         * Sets up camera data with the currently active camera and 
         * viewport and draw each renderer with its material.
         */
        void DrawRenderers(const std::string & tag, const RendererList &renderers);

        /**
         * @brief Draw renderers in the RendererList with specified pass index.
         * 
         * Sets up camera data and viewport and draw each renderer with its material.
         * 
         * Camera data are submitted to the buffer of the currently active camera
         * obtained from `GetActiveCameraId()` method. This might interfere with
         * other camera's matrices if not used properly. Therefore it is
         * suggested to use the simpler overload which handles camera data
         * internally.
         */
        void DrawRenderers(
            const std::string & tag,
            const RendererList &renderers,
            const glm::mat4 &view_matrix,
            const glm::mat4 &projection_matrix,
            vk::Extent2D extent
        );

        /// @brief End the render pass
        void EndRendering();

        void Reset() noexcept override;

    protected:
        uint32_t m_inflight_frame_index;
        std::optional<std::pair<vk::Pipeline, vk::PipelineLayout>> m_bound_material_pipeline{};
    };
} // namespace Engine

#endif // PIPELINE_COMMANDBUFFER_GRAPHICSCOMMANDBUFFER_INCLUDED
