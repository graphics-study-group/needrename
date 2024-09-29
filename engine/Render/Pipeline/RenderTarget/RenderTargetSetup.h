#ifndef PIPELINE_RENDERTARGET_RENDERTARGETSETUP_INCLUDED
#define PIPELINE_RENDERTARGET_RENDERTARGETSETUP_INCLUDED

#include <vulkan/vulkan.hpp>
#include "Render/Pipeline/RenderTarget/RenderPass.h"
#include "Render/Pipeline/RenderTarget/Framebuffers.h"

namespace Engine {
    class ImageInterface;
    /// @brief Render target setup.
    /// Automatically manages render pass and frame buffers.
    class RenderTargetSetup {
        std::weak_ptr <RenderSystem> m_system;
        std::unique_ptr <RenderPass> m_renderpass;
        Framebuffers m_framebuffers;

        void CreateRenderPassFromSwapchain();

    public:
        RenderTargetSetup(std::shared_ptr <RenderSystem> system);

        void CreateFromSwapchain();
        void Create(const ImagePerFrameInterface & color_targets, const ImagePerFrameInterface & depth_target);
        // void Create(std::vector< std::reference_wrapper<const ImageInterface> > render_targets, uint32_t frame_count);
        // void Create(std::vector< std::vector<std::reference_wrapper<const ImageInterface> > > render_targets_per_subpass, uint32_t frame_count);

        void SetClearValues(std::vector <vk::ClearValue> clear_values);
        const RenderPass & GetRenderPass() const;
        const Framebuffers & GetFramebuffers() const;
    };
}

#endif // PIPELINE_RENDERTARGET_RENDERTARGETSETUP_INCLUDED
