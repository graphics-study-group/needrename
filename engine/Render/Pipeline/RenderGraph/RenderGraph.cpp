#include "RenderGraph.h"

#include "Render/Pipeline/RenderGraph/RenderGraphTask.hpp"
#include "Render/RenderSystem/FrameManager.h"

#include <SDL3/SDL.h>

namespace Engine {
    struct RenderGraph::impl {
        std::vector <RenderGraphImpl::Task> m_commands;

        struct {
            RenderTargetTexture * target {nullptr};
            vk::Extent2D extent {};
            vk::Offset2D offset {};
        } present_info;
    };

    RenderGraph::RenderGraph(RenderSystem & system, std::vector<RenderGraphImpl::Task> commands) :
        m_system(system),
        pimpl(std::make_unique<impl>()) {
        for (const auto & t : commands) {
            if (auto ptr = std::get_if<RenderGraphImpl::Present>(&t)) {
                if (pimpl->present_info.target) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Multiple present info found in the RenderGraph.");
                }

                pimpl->present_info.target = &ptr->present_from;
                pimpl->present_info.extent = ptr->extent;
                pimpl->present_info.offset = ptr->offset;
            }
        }

        if (!pimpl->present_info.target) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "No present info found in the RenderGraph.");
        }

        pimpl->m_commands = std::move(commands);
    }
    RenderGraph::~RenderGraph() = default;

    void RenderGraph::Execute() {
        auto cb = m_system.GetFrameManager().GetRawMainCommandBuffer();
        vk::CommandBufferBeginInfo cbbi{};
        cb.begin(cbbi);

        for (const auto &f : pimpl->m_commands) {
            if (auto ptr = std::get_if<RenderGraphImpl::Command>(&f)) {
                std::invoke(ptr->func, cb);
            } else if (auto ptr = std::get_if<RenderGraphImpl::Synchronization>(&f)) {
                cb.pipelineBarrier2(
                    vk::DependencyInfo{
                        vk::DependencyFlags{},
                        ptr->memory_barriers,
                        ptr->buffer_barriers,
                        ptr->image_barriers
                    }
                );
            }
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
