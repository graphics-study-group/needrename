#include "RenderGraph.h"

#include "Render/Pipeline/RenderGraph/RenderGraphUtils.hpp"
#include "Render/Pipeline/RenderGraph/RenderGraphTask.hpp"
#include "Render/RenderSystem/FrameManager.h"

#include <SDL3/SDL.h>

namespace {
    inline vk::ImageMemoryBarrier2 GetImageBarrier(const Engine::Texture &texture,
        Engine::RenderGraphImpl::ImageAccessTuple old_access,
        Engine::RenderGraphImpl::ImageAccessTuple new_access) noexcept {
        using namespace Engine;
        vk::ImageMemoryBarrier2 barrier{};
        barrier.image = texture.GetImage();
        barrier.subresourceRange = vk::ImageSubresourceRange{
            ImageUtils::GetVkAspect(texture.GetTextureDescription().format),
            0,
            vk::RemainingMipLevels,
            0,
            vk::RemainingArrayLayers
        };

        if (barrier.subresourceRange.aspectMask == vk::ImageAspectFlagBits::eNone) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to infer aspect range when inserting an image barrier.");
            barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor | vk::ImageAspectFlagBits::eDepth
                                                    | vk::ImageAspectFlagBits::eStencil;
        }

        std::tie(barrier.srcStageMask, barrier.srcAccessMask, barrier.oldLayout) = old_access;
        std::tie(barrier.dstStageMask, barrier.dstAccessMask, barrier.newLayout) = new_access;

        barrier.dstQueueFamilyIndex = barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
        return barrier;
    }
}

namespace Engine {
    struct RenderGraph::impl {
        std::vector <std::function<void(vk::CommandBuffer)>> m_commands;

        RenderGraphImpl::Synchronization initial_sync, final_sync; 

        RenderGraphImpl::RenderGraphExtraInfo extra;

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
        pimpl->extra = std::move(extra);
    }
    RenderGraph::~RenderGraph() = default;

    void RenderGraph::AddExternalInputDependency(Texture &texture, MemoryAccessTypeImageBits previous_access) {
        auto itr = pimpl->extra.m_initial_image_access.find(&texture);
        if (itr == pimpl->extra.m_initial_image_access.end() || itr->second.second == MemoryAccessTypeImage{previous_access})
            return;

        pimpl->initial_sync.image_barriers.push_back(
            RenderGraphImpl::TextureAccessMemo::GenerateBarrier(
                texture.GetImage(),
                {previous_access}, RenderGraphImpl::PassType::None,
                itr->second.second, itr->second.first,
                ImageUtils::GetVkAspect(texture.GetTextureDescription().format)
            )
        );
    }

    void RenderGraph::AddExternalOutputDependency(Texture &texture, MemoryAccessTypeImageBits next_access) {
        auto itr = pimpl->extra.m_final_image_access.find(&texture);
        if (itr == pimpl->extra.m_final_image_access.end() || itr->second.second == MemoryAccessTypeImage{next_access})
            return;

        pimpl->final_sync.image_barriers.push_back(
            RenderGraphImpl::TextureAccessMemo::GenerateBarrier(
                texture.GetImage(),
                itr->second.second, itr->second.first,
                {next_access}, RenderGraphImpl::PassType::None,
                ImageUtils::GetVkAspect(texture.GetTextureDescription().format)
            )
        );
    }

    void RenderGraph::Record(vk::CommandBuffer cb) {
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
    }

    void RenderGraph::Execute() {
        auto cb = m_system.GetFrameManager().GetRawMainCommandBuffer();
        Record(cb);
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
