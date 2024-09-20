#ifndef RENDER_PIPELINE_RENDERPASS_INCLUDED
#define RENDER_PIPELINE_RENDERPASS_INCLUDED

#include "Render/VkWrapper.tcc"
#include <vulkan/vulkan.hpp>

#include "Render/Pipeline/RenderTarget/Framebuffers.h"

namespace Engine
{
    class Framebuffers;
    
    struct Subpass {
        vk::RenderPass pass;
        uint32_t index;
    };  

    /// @brief A Vulkan render pass.
    /// Render passes define data-flow within an actual rasterization pipeline,
    /// in terms of attachments (basically control blocks for image data), subpasses and dependencies.
    class RenderPass : public VkWrapper <vk::UniqueRenderPass>
    {
    protected:
        std::vector <vk::AttachmentDescription> m_attachments {};
        std::vector <vk::SubpassDescription> m_subpasses {};
        std::vector <vk::SubpassDependency> m_dependencies {};
        std::vector <vk::ClearValue> m_clear_values {};
        Framebuffers m_framebuffers;
    public:
        RenderPass (std::weak_ptr <RenderSystem> system);

        void CreateRenderPass();

        void CreateFramebuffersFromSwapchain();
        Subpass GetSubpass(uint32_t index) const;

        RenderPass & SetAttachments(std::vector <vk::AttachmentDescription> attachments);
        RenderPass & SetSubpasses(std::vector <vk::SubpassDescription> subpasses);
        RenderPass & SetDependencies(std::vector <vk::SubpassDependency> dependencies);
        RenderPass & SetClearValues(std::vector <vk::ClearValue> clear_values);

        auto GetAttachments() const -> const decltype(m_attachments) &;
        auto GetClearValues() const -> const decltype(m_clear_values) &;

        const Framebuffers & GetFramebuffers() const;
    protected:
    };
} // namespace Engine


#endif // RENDER_PIPELINE_RENDERPASS_INCLUDED
