#ifndef RENDERGRAPH_RENDERGRAPH
#define RENDERGRAPH_RENDERGRAPH

#include <deque>
#include <functional>

namespace vk {
    class CommandBuffer;
}

namespace Engine {
    class FrameManager;

    /**
     * @brief Resolved render graph ready to be executed.
     * Contains a list of `vk::CommandBuffer` method calls.
     * 
     * Dependencies should be resolved when building the render graph.
     */
    class RenderGraph {
        struct impl;
        std::unique_ptr <impl> pimpl;
    public:
        RenderGraph(std::deque <std::function<void(vk::CommandBuffer)>> commands);

        void Execute(FrameManager & system);
    };
}

#endif // RENDERGRAPH_RENDERGRAPH
