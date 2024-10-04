#include "RenderTargetSetup.h"

namespace Engine {


    RenderTargetSetup::RenderTargetSetup(std::shared_ptr<RenderSystem> system) : m_swapchain(system->GetSwapchain())
    {
    }

    void RenderTargetSetup::CreateFromSwapchain()
    {
        m_color_target = std::make_shared<SwapchainImage>(m_swapchain.GetColorImagesAndViews());
        m_depth_target = std::make_shared<SwapchainImage>(m_swapchain.GetDepthImagesAndViews());
    }

    void RenderTargetSetup::Create(const ImagePerFrameInterface &color_targets, const ImagePerFrameInterface &depth_target)
    {
    }

    void RenderTargetSetup::SetClearValues(std::vector<vk::ClearValue> clear_values)
    {
        assert(clear_values.size() == 2);
        m_clear_values = clear_values;
    }

    std::pair<vk::Image, vk::ImageView> RenderTargetSetup::GetColorAttachment(uint32_t frame_id, uint32_t index) const
    {
        assert(m_color_target);
        return std::make_pair(m_color_target->GetImage(frame_id), m_color_target->GetImageView(frame_id));
    }

    std::pair<vk::Image, vk::ImageView> RenderTargetSetup::GetDepthAttachment(uint32_t frame_id) const
    {
        assert(m_depth_target);
        return std::make_pair(m_depth_target->GetImage(frame_id), m_depth_target->GetImageView(frame_id));
    }

    const std::vector<vk::ClearValue> &RenderTargetSetup::GetClearValues() const
    {
        return m_clear_values;
    }
}
