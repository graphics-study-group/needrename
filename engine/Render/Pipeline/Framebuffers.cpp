#include "Framebuffers.h"

#include "Render/Pipeline/RenderPass.h"

namespace Engine
{
    Framebuffers::Framebuffers(std::weak_ptr<RenderSystem> system) : m_system(system) {
    }

    void Framebuffers::CreateFramebuffers(const RenderPass& pass) {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating framebuffers.");
        m_framebuffers.clear();

        auto system = m_system.lock();
        const auto & swapchain = system->getSwapchainInfo();
        m_framebuffers.reserve(swapchain.imageViews.size());
        for (size_t i = 0; i < swapchain.imageViews.size(); i++) {
            std::vector <vk::ImageView> imageViews = { swapchain.imageViews[i].get() };
            vk::FramebufferCreateInfo info{};
            info.renderPass = pass.get();
            info.attachmentCount = imageViews.size();
            info.pAttachments = imageViews.data();
            info.setWidth(swapchain.extent.width).setHeight(swapchain.extent.height);
            info.layers = 1;
            m_framebuffers.emplace_back(system->getDevice().createFramebufferUnique(info));
        }
        m_framebuffers.shrink_to_fit();
    }

    
} // namespace Engine

