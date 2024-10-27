#include "RenderTargetSetup.h"

#include "Render/RenderSystem.h"

namespace Engine {
    RenderTargetSetup::RenderTargetSetup(std::shared_ptr<RenderSystem> system) : m_swapchain(system->GetSwapchain())
    {
    }

    void RenderTargetSetup::CreateFromSwapchain()
    {
        m_color_targets = {std::make_shared<SwapchainImage>(m_swapchain.GetColorImagesAndViews())};
        m_depth_target = std::make_shared<SwapchainImage>(m_swapchain.GetDepthImagesAndViews());
        m_present_attachment_id = 0;
    }

    void RenderTargetSetup::Create(const RenderImageTexture &color_targets, const RenderImageTexture &depth_target)
    {
    }

    void RenderTargetSetup::SetClearValues(std::vector<vk::ClearValue> clear_values)
    {
        // TODO: Better clear values and attachment matching
        assert( 
            (clear_values.size() == m_color_targets.size() && !m_depth_target) || 
            (clear_values.size() == m_color_targets.size() + 1 && m_depth_target)
        );
        m_clear_values = clear_values;
    }

    AttachmentUtils::AttachmentDescription RenderTargetSetup::GetColorAttachment(uint32_t frame_id, uint32_t index) const
    {
        assert(index < m_color_targets.size() && m_color_targets[index]);
        return {
            m_color_targets[index]->GetImage(frame_id), 
            m_color_targets[index]->GetImageView(frame_id),
            AttachmentUtils::GetVkLoadOp(m_color_targets[index]->GetLoadOperation()),
            AttachmentUtils::GetVkStoreOp(m_color_targets[index]->GetStoreOperation())
        };
    }

    vk::Image RenderTargetSetup::GetImageForPresentation(uint32_t frame_id) const
    {
        if (m_present_attachment_id < m_color_targets.size()) {
            return m_color_targets[m_present_attachment_id]->GetImage(frame_id);
        } else if (m_present_attachment_id == m_color_targets.size()) {
            return m_depth_target->GetImage(frame_id);
        }
        return nullptr;
    }

    size_t RenderTargetSetup::GetColorAttachmentSize() const
    {
        return m_color_targets.size();
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
