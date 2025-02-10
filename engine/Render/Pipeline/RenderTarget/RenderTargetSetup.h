#ifndef PIPELINE_RENDERTARGET_RENDERTARGETSETUP_INCLUDED
#define PIPELINE_RENDERTARGET_RENDERTARGETSETUP_INCLUDED

#include <vulkan/vulkan.hpp>
#include "Render/RenderSystem/Swapchain.h"
#include "Render/Memory/RenderImageTexture.h"

namespace Engine {
    class ImagePerFrameInterface;
    /// @brief Render target setup.
    /// Automatically manages render pass and frame buffers.
    /// TODO: Load, store and clear values are to be integrated into MaterialTemplate
    class RenderTargetSetup {
        const RenderSystemState::Swapchain & m_swapchain;
        std::vector <std::shared_ptr <const RenderImageTexture>> m_color_targets {};
        std::shared_ptr <const RenderImageTexture> m_depth_target {nullptr};
        std::vector <vk::ClearValue> m_clear_values {};

        int32_t m_present_attachment_id{};
    public:
        RenderTargetSetup(std::shared_ptr <RenderSystem> system);

        void CreateFromSwapchain();
        void Create(const RenderImageTexture & color_targets, const RenderImageTexture & depth_target);
        void SetClearValues(std::vector <vk::ClearValue> clear_values);

        vk::Image GetImageForPresentation(uint32_t frame_id) const;
        size_t GetColorAttachmentSize() const;
        AttachmentUtils::AttachmentDescription GetColorAttachment(uint32_t frame_id, uint32_t index = 0) const;
        AttachmentUtils::AttachmentDescription GetDepthAttachment(uint32_t frame_id) const;
        const std::vector <vk::ClearValue> & GetClearValues() const;
    };
}

#endif // PIPELINE_RENDERTARGET_RENDERTARGETSETUP_INCLUDED
