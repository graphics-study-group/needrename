#include "RenderTargetSetup.h"

#include "Render/RenderSystem.h"

namespace Engine {
    RenderTargetSetup::RenderTargetSetup(std::shared_ptr<RenderSystem> system) : m_swapchain(system->GetSwapchain())
    {
    }

    void RenderTargetSetup::CreateFromSwapchain()
    {
        m_color_target = std::make_shared<SwapchainImage>(m_swapchain.GetColorImagesAndViews());
        m_depth_target = std::make_shared<SwapchainImage>(m_swapchain.GetDepthImagesAndViews());
    }

    void RenderTargetSetup::Create(const RenderImageTexture &color_targets, const RenderImageTexture &depth_target)
    {
    }

    void RenderTargetSetup::SetClearValues(std::vector<vk::ClearValue> clear_values)
    {
        // TODO: Better clear values and attachment matching
        assert( 
            (clear_values.size() == 1 && !m_swapchain.IsDepthEnabled()) || 
            (clear_values.size() == 2 && m_swapchain.IsDepthEnabled())
        );
        m_clear_values = clear_values;
    }

    AttachmentUtils::AttachmentDescription RenderTargetSetup::GetColorAttachment(uint32_t frame_id, uint32_t index) const
    {
        assert(m_color_target);
        return {
            m_color_target->GetImage(frame_id), 
            m_color_target->GetImageView(frame_id),
            AttachmentUtils::GetVkLoadOp(m_color_target->GetLoadOperation()),
            AttachmentUtils::GetVkStoreOp(m_color_target->GetStoreOperation())
        };
    }

    AttachmentUtils::AttachmentDescription RenderTargetSetup::GetDepthAttachment(uint32_t frame_id) const
    {
        assert(m_depth_target);
        return {
            m_depth_target->GetImage(frame_id), 
            m_depth_target->GetImageView(frame_id),
            AttachmentUtils::GetVkLoadOp(m_depth_target->GetLoadOperation()),
            AttachmentUtils::GetVkStoreOp(m_depth_target->GetStoreOperation())
        };
    }

    const std::vector<vk::ClearValue> &RenderTargetSetup::GetClearValues() const
    {
        return m_clear_values;
    }
}
