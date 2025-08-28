#include "RenderGraph.h"

#include "Render/RenderSystem/FrameManager.h"

namespace Engine {
    struct RenderGraph::impl {
        std::vector<std::function<void(vk::CommandBuffer)>> m_commands;
    };

    RenderGraph::RenderGraph(std::vector<std::function<void(vk::CommandBuffer)>> commands) :
        pimpl(std::make_unique<impl>(commands)) {
    }
    RenderGraph::~RenderGraph() = default;

    void RenderGraph::Execute(RenderSystemState::FrameManager &mgr) {
        auto cb = mgr.GetRawMainCommandBuffer();
        vk::CommandBufferBeginInfo cbbi{};
        cb.begin(cbbi);

        for (const auto &f : pimpl->m_commands) {
            std::invoke(f, cb);
        }

        cb.end();
        mgr.SubmitMainCommandBuffer();
    }

} // namespace Engine
