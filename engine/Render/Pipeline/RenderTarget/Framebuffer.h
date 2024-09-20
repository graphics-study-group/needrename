#ifndef PIPELINE_RENDERTARGET_FRAMEBUFFER_INCLUDED
#define PIPELINE_RENDERTARGET_FRAMEBUFFER_INCLUDED

#include "Render/VkWrapper.tcc"

namespace Engine {
    class ImageInterface;
    class RenderPass;

    class Framebuffer : public VkWrapper <vk::UniqueFramebuffer> {
    public:
        Framebuffer(std::weak_ptr <RenderSystem> system);

        void Create(const RenderPass & pass, vk::Extent2D extent, std::vector <vk::ImageView> attachments);
    };
}

#endif // PIPELINE_RENDERTARGET_FRAMEBUFFER_INCLUDED
