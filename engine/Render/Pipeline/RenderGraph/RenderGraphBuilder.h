#ifndef RENDERGRAPH_RENDERGRAPHBUILDER
#define RENDERGRAPH_RENDERGRAPHBUILDER

#include <memory>
#include <functional>

#include "Render/Pipeline/CommandBuffer/AccessHelperTypes.h"

namespace Engine {

    class RenderSystem;
    class GUISystem;
    class Texture;
    class Buffer;
    class RenderGraph;
    class GraphicsCommandBuffer;
    class TransferCommandBuffer;
    class ComputeCommandBuffer;

    /**
     * @brief Helper class for building a `RenderGraph`.
     * 
     * Helps to resolve data dependencies between passes and set up barriers.
     */
    class RenderGraphBuilder {
        RenderSystem & m_system;
        struct impl;
        std::unique_ptr <impl> pimpl;
    public:

        RenderGraphBuilder(RenderSystem & system);
        ~RenderGraphBuilder();
        
        /**
         * @brief Mark an image to be used in the following pass.
         */
        void UseImage (
            Texture & texture,
            AccessHelper::ImageAccessType new_access, 
            AccessHelper::ImageAccessType prev_access
        );

        /**
         * @brief Mark a buffer to be used in the following pass.
         */
        void UseBuffer (
            Buffer & buffer,
            AccessHelper::BufferAccessType new_access,
            AccessHelper::BufferAccessType prev_access
        );

        /**
         * @brief Record a pass which includes exactly one draw call.
         */
        void RecordRasterizerPass (std::function<void(GraphicsCommandBuffer &)> pass);

        /**
         * @brief Record a pass with transfer commands.
         */
        void RecordTransferPass (std::function<void(TransferCommandBuffer &)> pass);

        /**
         * @brief Record a pass with exactly one compute shader dispatch.
         */
        void RecordComputePass (std::function<void(ComputeCommandBuffer &)> pass);

        /**
         * @brief Explicitly record a synchronization operation.
         */
        void RecordSynchronization ();

        /**
         * @brief Create a RenderGraph and reset internal states to default.
         */
        RenderGraph BuildRenderGraph();

        /**
         * @brief Build a default render graph.
         */
        RenderGraph BuildDefaultRenderGraph(
            Texture & color_attachment, 
            Texture & depth_attachment,
            GUISystem * gui_system = nullptr
        );
    };
}

#endif // RENDERGRAPH_RENDERGRAPHBUILDER
