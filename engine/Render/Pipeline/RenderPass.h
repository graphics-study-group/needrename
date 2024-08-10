#ifndef RENDER_PIPELINE_RENDERPASS_INCLUDED
#define RENDER_PIPELINE_RENDERPASS_INCLUDED

#include "Render/VkWrapper.tcc"
#include <vulkan/vulkan.hpp>



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
        void CreateRenderPass(
            std::vector <vk::AttachmentDescription> attachments, 
            std::vector <vk::SubpassDescription> subpasses, 
            std::vector <vk::SubpassDependency> dependencies
        );
        Framebuffers CreateFramebuffers();
        Subpass GetSubpass(uint32_t index);
    };
} // namespace Engine


#endif // RENDER_PIPELINE_RENDERPASS_INCLUDED
