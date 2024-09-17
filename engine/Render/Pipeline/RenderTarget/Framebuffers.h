#ifndef RENDER_PIPELINE_FRAMEBUFFERS_INCLUDED
#define RENDER_PIPELINE_FRAMEBUFFERS_INCLUDED

#include "Render/RenderSystem.h"
#include "Render/Pipeline/RenderTarget/Framebuffer.h"

namespace Engine
{
    class RenderPass;
    /// @brief Framebuffers attached to a render pass
    class Framebuffers
    {
    public:
        Framebuffers(std::weak_ptr <RenderSystem> system);

        /// @brief Create framebuffers, the number of which is the same as frames in the swapchain.
        /// The first attachment of the render pass is set to swapchain image, and the second is set to depth image.
        /// @param pass 
        void CreateFramebuffersFromSwapchain(const RenderPass & pass);

        vk::Framebuffer GetFramebuffer(uint32_t index) const;
    protected:
        std::weak_ptr <RenderSystem> m_system;
        std::vector <Framebuffer> m_framebuffers {};
    };
}

#endif // RENDER_PIPELINE_FRAMEBUFFERS_INCLUDED
