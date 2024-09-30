#ifndef PIPELINE_RENDERTARGET_RENDERTARGETSETUP_INCLUDED
#define PIPELINE_RENDERTARGET_RENDERTARGETSETUP_INCLUDED

#include <vulkan/vulkan.hpp>
#include "Render/RenderSystem.h"

namespace Engine {
    class ImagePerFrameInterface;
    class RenderSystemState::Swapchain;
    /// @brief Render target setup.
    /// Automatically manages render pass and frame buffers.
    class RenderTargetSetup {
        const RenderSystemState::Swapchain & m_swapchain;
        std::shared_ptr <const ImagePerFrameInterface> m_color_target {nullptr};
        std::shared_ptr <const ImagePerFrameInterface> m_depth_target {nullptr};
        std::vector <vk::ClearValue> m_clear_values {};
    public:
        RenderTargetSetup(std::shared_ptr <RenderSystem> system);

        void CreateFromSwapchain();
        void Create(const ImagePerFrameInterface & color_targets, const ImagePerFrameInterface & depth_target);
        void SetClearValues(std::vector <vk::ClearValue> clear_values);

        std::pair<vk::Image, vk::ImageView> GetColorAttachment(uint32_t frame_id, uint32_t index = 0) const;
        std::pair<vk::Image, vk::ImageView> GetDepthAttachment(uint32_t frame_id) const;
        const std::vector <vk::ClearValue> & GetClearValues() const;
    };
}

#endif // PIPELINE_RENDERTARGET_RENDERTARGETSETUP_INCLUDED
