#ifndef PIPELINE_RENDERGRAPH_RENDERGRAPHBUILDER_INCLUDED
#define PIPELINE_RENDERGRAPH_RENDERGRAPHBUILDER_INCLUDED

#include <memory>
#include <functional>

#include "Render/Memory/MemoryAccessTypes.h"
#include "Render/Pipeline/CommandBuffer/AccessHelperTypes.h"

namespace Engine {

    class RenderSystem;
    class GUISystem;
    class Texture;
    class DeviceBuffer;
    class RenderGraph;
    class GraphicsCommandBuffer;
    class TransferCommandBuffer;
    class ComputeCommandBuffer;

    namespace AttachmentUtils {
        class AttachmentDescription;
    };

    /**
     * @brief Helper class for building a `RenderGraph`.
     * 
     * Helps to resolve data dependencies between passes and set up barriers.
     */
    class RenderGraphBuilder {
        struct impl;
        std::unique_ptr <impl> pimpl;
    protected:
        RenderSystem &m_system;
    public:

        RenderGraphBuilder(RenderSystem & system);
        ~RenderGraphBuilder();

        /**
         * @brief Register a new image texture to manage its access by the internal memo system.
         */
        void ImportExternalResource (
            const Texture & texture,
            MemoryAccessTypeImageBits prev_access = MemoryAccessTypeImageBits::None
        );
        
        /**
         * @brief Mark an image to be used in the following pass.
         */
        void UseImage (
            const Texture & texture,
            MemoryAccessTypeImageBits access
        );

        /**
         * @brief Mark a buffer to be used in the following pass.
         */
        void UseBuffer (
            const DeviceBuffer & buffer,
            MemoryAccessTypeBuffer access
        );

        /**
         * @brief Record a pass which includes draw calls.
         * 
         * To use this method, you have to manually set up render targets within the `pass` function.
         */
        void RecordRasterizerPassWithoutRT (
            std::function<void(GraphicsCommandBuffer &)> pass
        );

        /**
         * @brief Record a pass which includes draw calls.
         * 
         * Depth attachment is defaulted to null.
         * This method automatically begins and terminates rendering.
         * Its extent is set to swapchain extent.
         */
        void RecordRasterizerPass (
            AttachmentUtils::AttachmentDescription color,
            std::function<void(GraphicsCommandBuffer &)> pass,
            const std::string & name = ""
        );

        /**
         * @brief Record a pass which includes draw calls.
         * 
         * This method automatically begins and terminates rendering.
         * Its extent is set to swapchain extent.
         */
        void RecordRasterizerPass (
            AttachmentUtils::AttachmentDescription color,
            AttachmentUtils::AttachmentDescription depth,
            std::function<void(GraphicsCommandBuffer &)> pass,
            const std::string & name = ""
        );

        /**
         * @brief Record a pass which includes draw calls.
         * 
         * This method automatically begins and terminates rendering.
         * Its extent is set to swapchain extent.
         */
        void RecordRasterizerPass (
            std::initializer_list <AttachmentUtils::AttachmentDescription> colors,
            AttachmentUtils::AttachmentDescription depth,
            std::function<void(GraphicsCommandBuffer &)> pass,
            const std::string & name = ""
        );

        /**
         * @brief Record a pass with transfer commands.
         */
        void RecordTransferPass (
            std::function<void(TransferCommandBuffer &)> pass,
            const std::string & name = ""
        );

        /**
         * @brief Record a pass with compute shader dispatches.
         */
        void RecordComputePass (
            std::function<void(ComputeCommandBuffer &)> pass,
            const std::string & name = ""
        );

        /**
         * @brief Explicitly record a synchronization operation.
         */
        void RecordSynchronization ();

        /**
         * @brief Create a RenderGraph and reset internal states to default.
         */
        std::unique_ptr<RenderGraph> BuildRenderGraph();

        /**
         * @brief Build a default render graph.
         */
        std::unique_ptr<RenderGraph> BuildDefaultRenderGraph(
            const RenderTargetTexture & color_attachment, 
            const RenderTargetTexture & depth_attachment,
            GUISystem * gui_system = nullptr
        );
    };
}

#endif // PIPELINE_RENDERGRAPH_RENDERGRAPHBUILDER_INCLUDED
