#include "RenderGraph.h"

#include "Render/Pipeline/RenderGraph/RenderGraphUtils.hpp"
#include "Render/Pipeline/RenderGraph/RenderGraphTask.hpp"
#include "Render/RenderSystem/FrameManager.h"

#include <SDL3/SDL.h>

namespace Engine {
    struct RenderGraph::impl {
        std::vector <std::function<void(vk::CommandBuffer)>> m_commands;

        RenderGraphImpl::Synchronization initial_sync, final_sync; 

        std::unordered_map <const Texture *, RenderGraphImpl::ImageAccessTuple> m_initial_image_access;
        std::unordered_map <const Texture *, RenderGraphImpl::ImageAccessTuple> m_final_image_access;

        struct {
            RenderTargetTexture * target {nullptr};
            vk::Extent2D extent {};
            vk::Offset2D offset {};
        } present_info;
    };

    RenderGraph::RenderGraph(
        RenderSystem & system,
        std::vector<std::function<void(vk::CommandBuffer)>> && commands,
        RenderGraphImpl::RenderGraphExtraInfo && extra
    ) :
        m_system(system),
        pimpl(std::make_unique<impl>()) {
        pimpl->m_commands = std::move(commands);
        pimpl->m_initial_image_access = std::move(extra.m_initial_image_access);
        pimpl->m_final_image_access = std::move(extra.m_final_image_access);
    }
    RenderGraph::~RenderGraph() = default;

    void RenderGraph::AddExternalInputDependency(Texture &texture, AccessHelper::ImageAccessType previous_access) {
        auto itr = pimpl->m_initial_image_access.find(&texture);
        auto access_tuple = AccessHelper::GetAccessScope(previous_access);
        if (itr == pimpl->m_initial_image_access.end() || itr->second == access_tuple)
            return;

        pimpl->initial_sync.image_barriers.push_back(RenderGraphImpl::GetImageBarrier(texture, access_tuple, itr->second));
    }

    void RenderGraph::AddExternalOutputDependency(Texture &texture, AccessHelper::ImageAccessType next_access) {
        auto itr = pimpl->m_final_image_access.find(&texture);
        auto access_tuple = AccessHelper::GetAccessScope(next_access);
        if (itr == pimpl->m_final_image_access.end() || itr->second == access_tuple)
            return;

        pimpl->final_sync.image_barriers.push_back(RenderGraphImpl::GetImageBarrier(texture, itr->second, access_tuple));
    }

    void RenderGraph::Execute() {
        auto cb = m_system.GetFrameManager().GetRawMainCommandBuffer();
        vk::CommandBufferBeginInfo cbbi{};
        cb.begin(cbbi);

        if (!pimpl->initial_sync.empty()) {
            std::invoke(pimpl->initial_sync.GetBarrierCommand(), cb);
            pimpl->initial_sync.clear();
        }

        for (const auto &f : pimpl->m_commands) {
            std::invoke(f, cb);
        }

        if (!pimpl->final_sync.empty()) {
            std::invoke(pimpl->final_sync.GetBarrierCommand(), cb);
            pimpl->final_sync.clear();
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
