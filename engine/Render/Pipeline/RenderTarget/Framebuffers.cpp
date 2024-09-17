#include "Framebuffers.h"

#include "Render/Pipeline/RenderTarget/Framebuffer.h"
#include "Render/Pipeline/RenderPass.h"

namespace Engine
{
    Framebuffers::Framebuffers(std::weak_ptr<RenderSystem> system) : m_system(system) {
    }

    void Framebuffers::CreateFramebuffersFromSwapchain(const RenderPass& pass) {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating framebuffers.");
        m_framebuffers.clear();

        auto system = m_system.lock();
        const auto & swapchain = system->GetSwapchain();
        const std::vector <AllocatedImage2D> & depth_images = swapchain.GetDepthImages();
        vk::Extent2D extent = swapchain.GetExtent();

        uint32_t frame_count = swapchain.GetFrameCount();
        for (size_t i = 0; i < frame_count; i++) {
            std::vector <SwapchainImage> images;
            // XXX: We need to design a better scheme for mapping attachments to images
            if (swapchain.IsDepthEnabled()) {
                images = { swapchain.GetImage(i), swapchain.GetDepthImage(i) };
                m_framebuffers.emplace_back(system);
                m_framebuffers[i].Create(pass, swapchain.GetExtent(), {std::cref(images[0]), std::cref(images[1])});
            } else {
                images = { swapchain.GetImage(i) };
                m_framebuffers.emplace_back(system);
                m_framebuffers[i].Create(pass, swapchain.GetExtent(), {std::cref(images[0])});
            }
        }
    }

    vk::Framebuffer Framebuffers::GetFramebuffer(uint32_t index) const {
        assert(m_framebuffers.size() > index);
        return m_framebuffers[index].get();
    }
} // namespace Engine

