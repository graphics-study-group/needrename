#ifndef RENDER_PIPELINE_RENDERPASS_INCLUDED
#define RENDER_PIPELINE_RENDERPASS_INCLUDED

#include "Render/VkWrapper.tcc"
#include <vulkan/vulkan.hpp>

#include "Render/Pipeline/Framebuffers.h"

namespace Engine
{
    class Framebuffers;
    
    struct Subpass {
        vk::RenderPass pass;
        uint32_t index;
    };  

    class RenderPass : public VkWrapper <vk::UniqueRenderPass>
    {
    public:
        RenderPass (std::weak_ptr <RenderSystem> system);

        void CreateRenderPass();

        void CreateFramebuffers();
        Subpass GetSubpass(uint32_t index);

        RenderPass & SetAttachments(std::vector <vk::AttachmentDescription> attachments);
        RenderPass & SetSubpasses(std::vector <vk::SubpassDescription> subpasses);
        RenderPass & SetDependencies(std::vector <vk::SubpassDependency> dependencies);

        const std::vector <vk::AttachmentDescription> & GetAttachments() const;

        const Framebuffers & GetFramebuffers() const;
    protected:
        std::vector <vk::AttachmentDescription> m_attachments {};
        std::vector <vk::SubpassDescription> m_subpasses {};
        std::vector <vk::SubpassDependency> m_dependencies {};

        Framebuffers m_framebuffers;
    };
} // namespace Engine


#endif // RENDER_PIPELINE_RENDERPASS_INCLUDED
