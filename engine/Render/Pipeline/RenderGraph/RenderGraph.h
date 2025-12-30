#ifndef PIPELINE_RENDERGRAPH_RENDERGRAPH_INCLUDED
#define PIPELINE_RENDERGRAPH_RENDERGRAPH_INCLUDED

#include <vector>
#include <functional>

namespace vk {
    class CommandBuffer;
}

namespace Engine {
    namespace RenderSystemState {
        class FrameManager;
    }

    namespace RenderGraphImpl {
        struct RenderGraphExtraInfo;
    }

    /**
     * @brief Resolved render graph ready to be executed.
     * Contains a list of `vk::CommandBuffer` method calls.
     * 
     * Dependencies should be resolved when building the render graph.
     */
    class RenderGraph {
        RenderSystem & m_system;
        
        struct impl;
        std::unique_ptr <impl> pimpl;

        friend class RenderGraphBuilder;

        RenderGraph(
            RenderSystem & system,
            std::vector <std::function<void(vk::CommandBuffer)>> && commands,
            const RenderGraphImpl::RenderGraphExtraInfo & extra
        );
    public:
        
        ~RenderGraph();

        /**
         * @brief Add an external input dependency on a texture for this frame.
         * 
         * Useful for setting up temporal reused textures.
         */
        void AddExternalInputDependency(Texture & texture, AccessHelper::ImageAccessType access);
        void AddExternalInputDependency(Buffer & buffer, AccessHelper::BufferAccessType access);

        /**
         * @brief Add an external output dependency on a texture for this frame.
         * 
         * Useful if you want to present an image that is not used as a color attachment.
         */
        void AddExternalOutputDependency(Texture & texture, AccessHelper::ImageAccessType access);
        void AddExternalOutputDependency(Buffer & buffer, AccessHelper::BufferAccessType access);

        void Execute();
    };
}

#endif // PIPELINE_RENDERGRAPH_RENDERGRAPH_INCLUDED
