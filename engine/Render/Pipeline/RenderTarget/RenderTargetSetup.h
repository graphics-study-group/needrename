#ifndef PIPELINE_RENDERTARGET_RENDERTARGET_INCLUDED
#define PIPELINE_RENDERTARGET_RENDERTARGET_INCLUDED

#include <vulkan/vulkan.hpp>
#include "Render/Pipeline/RenderTarget/Framebuffers.h"

namespace Engine {
    class ImageInterface;
    /// @brief Render target setup.
    /// Automatically manages render pass and frame buffers.
    class RenderTargetSetup {
        vk::RenderPass m_render_pass;
        std::vector<Framebuffers> m_framebuffers_per_subpass;

    public:
        void Create(std::vector< std::reference_wrapper<const ImageInterface> > render_targets, uint32_t frame_count);
        void Create(std::vector< std::vector<std::reference_wrapper<const ImageInterface> > > render_targets_per_subpass, uint32_t frame_count);
    };
}

#endif // PIPELINE_RENDERTARGET_RENDERTARGET_INCLUDED
