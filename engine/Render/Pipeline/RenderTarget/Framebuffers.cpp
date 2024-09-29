#include "Framebuffers.h"

#include "Render/Pipeline/RenderTarget/Framebuffer.h"
#include "Render/Pipeline/RenderTarget/RenderPass.h"

namespace Engine
{
    Framebuffers::Framebuffers(std::weak_ptr<RenderSystem> system) : m_system(system) {
    }

    void Framebuffers::CreateFramebuffersFromSwapchain(const RenderPass& pass) {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating framebuffers.");
        m_framebuffers.clear();

        auto system = m_system.lock();
        const auto & swapchain = system->GetSwapchain();
        auto color_images = swapchain.GetColorImagesAndViews();
        auto depth_images = swapchain.GetDepthImagesAndViews();
        vk::Extent2D extent = swapchain.GetExtent();

        uint32_t frame_count = swapchain.GetFrameCount();
        for (size_t i = 0; i < frame_count; i++) {
            std::vector <vk::ImageView> image_views;
            // XXX: We need to design a better scheme for mapping attachments to images
            if (swapchain.IsDepthEnabled()) {
                image_views = { color_images.GetImageView(i), depth_images.GetImageView(i) };
                m_framebuffers.emplace_back(system);
                m_framebuffers[i].Create(pass, swapchain.GetExtent(), image_views);
            } else {
                image_views = { color_images.GetImageView(i) };
                m_framebuffers.emplace_back(system);
                m_framebuffers[i].Create(pass, swapchain.GetExtent(), image_views);
            }
        }
    }

    vk::Framebuffer Framebuffers::GetFramebuffer(uint32_t index) const {
        assert(m_framebuffers.size() > index);
        return m_framebuffers[index].get();
    }
} // namespace Engine

