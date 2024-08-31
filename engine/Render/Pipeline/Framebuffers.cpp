#include "Framebuffers.h"

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
        const auto & image_views = swapchain.GetImageViews();
        vk::Extent2D extent = swapchain.GetExtent();
        m_framebuffers.reserve(image_views.size());
        for (size_t i = 0; i < image_views.size(); i++) {
            std::vector <vk::ImageView> imageViews = { image_views[i].get() };
            vk::FramebufferCreateInfo info{};
            info.renderPass = pass.get();
            info.attachmentCount = imageViews.size();
            info.pAttachments = imageViews.data();
            info.setWidth(extent.width).setHeight(extent.height);
            info.layers = 1;
            m_framebuffers.emplace_back(system->getDevice().createFramebufferUnique(info));
        }
        m_framebuffers.shrink_to_fit();
    }

    vk::Framebuffer Framebuffers::GetFramebuffer(uint32_t index) const {
        assert(m_framebuffers.size() > index);
        return m_framebuffers[index].get();
    }
} // namespace Engine

