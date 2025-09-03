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
    public:
        RenderGraph(RenderSystem & system, std::vector <std::function<void(vk::CommandBuffer)>> commands);
        ~RenderGraph();

        void Execute();
    };
}

#endif // PIPELINE_RENDERGRAPH_RENDERGRAPH_INCLUDED
