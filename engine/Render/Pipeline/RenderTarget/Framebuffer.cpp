#include "Framebuffer.h"

#include "Render/Pipeline/RenderPass.h"
#include "Render/Memory/ImageInterface.h"

namespace Engine {
    Framebuffer::Framebuffer(std::weak_ptr<RenderSystem> system) : VkWrapper(system)
    {
    }

    void Framebuffer::Create(const RenderPass &pass, vk::Extent2D extent, std::vector<std::reference_wrapper<const ImageInterface>> attachments)
    {
        std::vector <vk::ImageView> views {attachments.size(), vk::ImageView{}};
        for (size_t i = 0; i < attachments.size(); i++) {
            views[i] = attachments[i].get().GetImageView();
        }
        vk::FramebufferCreateInfo info{
            vk::FramebufferCreateFlags{},
            pass.get(),
            views,
            extent.width,
            extent.height,
            1U
        };

        m_handle = m_system.lock()->getDevice().createFramebufferUnique(info);
    }
}
