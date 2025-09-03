#include "RenderGraph.h"

#include "Render/RenderSystem/FrameManager.h"

namespace Engine {
    struct RenderGraph::impl {
        std::vector<std::function<void(vk::CommandBuffer)>> m_commands;
    };

    RenderGraph::RenderGraph(RenderSystem & system, std::vector<std::function<void(vk::CommandBuffer)>> commands) :
        m_system(system),
        pimpl(std::make_unique<impl>(commands)) {
    }
    RenderGraph::~RenderGraph() = default;

    void RenderGraph::Execute() {
        auto cb = m_system.GetFrameManager().GetRawMainCommandBuffer();
        vk::CommandBufferBeginInfo cbbi{};
        cb.begin(cbbi);

        for (const auto &f : pimpl->m_commands) {
            std::invoke(f, cb);
        }

        cb.end();
        m_system.GetFrameManager().SubmitMainCommandBuffer();
    }

} // namespace Engine
