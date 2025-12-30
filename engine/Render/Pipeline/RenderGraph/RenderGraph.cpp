#include "RenderGraph.h"

#include "Render/Pipeline/RenderGraph/RenderGraphTask.hpp"
#include "Render/RenderSystem/FrameManager.h"

#include <SDL3/SDL.h>

namespace Engine {
    struct RenderGraph::impl {
        std::vector <std::function<void(vk::CommandBuffer)>> m_commands;

        struct {
            RenderTargetTexture * target {nullptr};
            vk::Extent2D extent {};
            vk::Offset2D offset {};
        } present_info;
    };

    RenderGraph::RenderGraph(
        RenderSystem & system,
        std::vector<std::function<void(vk::CommandBuffer)>> && commands,
        const RenderGraphImpl::RenderGraphExtraInfo & extra
    ) :
        m_system(system),
        pimpl(std::make_unique<impl>()) {
        pimpl->m_commands = std::move(commands);
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
        if (pimpl->present_info.target)
            m_system.CompleteFrame(
                *pimpl->present_info.target,
                pimpl->present_info.extent.width,
                pimpl->present_info.extent.height,
                pimpl->present_info.offset.x,
                pimpl->present_info.offset.y
            );
    }

} // namespace Engine
