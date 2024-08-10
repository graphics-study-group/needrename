#ifndef RENDER_PIPELINE_FRAMEBUFFERS_INCLUDED
#define RENDER_PIPELINE_FRAMEBUFFERS_INCLUDED

#include "Render/RenderSystem.h"

namespace Engine
{
    class RenderPass;
    /// @brief Framebuffers attached to a render pass
    class Framebuffers
    {
    public:
        Framebuffers(std::weak_ptr <RenderSystem> system);
        void CreateFramebuffers(const RenderPass & pass);
    protected:
        std::weak_ptr <RenderSystem> m_system;
        std::vector <vk::UniqueFramebuffer> m_framebuffers {};
    };
}

#endif // RENDER_PIPELINE_FRAMEBUFFERS_INCLUDED
