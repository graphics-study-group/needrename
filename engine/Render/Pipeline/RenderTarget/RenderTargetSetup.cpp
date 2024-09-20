#include "RenderTargetSetup.h"

#include "Render/Pipeline/PremadePipeline/SingleRenderPass.h"
#include "Render/Pipeline/PremadePipeline/SingleRenderPassWithDepth.h"

namespace Engine {
    void RenderTargetSetup::CreateRenderPassFromSwapchain()
    {
        auto system = m_system.lock();
        if (system->GetSwapchain().IsDepthEnabled()) {
            SingleRenderPassWithDepth * ptr = new SingleRenderPassWithDepth(system);
            ptr->CreateRenderPass();
            m_renderpass.reset(ptr);
        } else {
            SingleRenderPass * ptr = new SingleRenderPass(system);
            ptr->CreateRenderPass();
            m_renderpass.reset(ptr);
        }
    }

    RenderTargetSetup::RenderTargetSetup(std::shared_ptr<RenderSystem> system) : m_system(system), m_renderpass(nullptr), m_framebuffers(system)
    {
    }

    void RenderTargetSetup::CreateFromSwapchain()
    {
        CreateRenderPassFromSwapchain();
        m_renderpass->CreateFramebuffersFromSwapchain();
    }

    void RenderTargetSetup::Create(const ImagePerFrameInterface &color_targets, const ImagePerFrameInterface &depth_target)
    {
    }

    void RenderTargetSetup::SetClearValues(std::vector<vk::ClearValue> clear_values)
    {
        m_renderpass->SetClearValues(clear_values);
    }

    const RenderPass & RenderTargetSetup::GetRenderPass() const
    {
        return *m_renderpass.get();
    }
    const Framebuffers &RenderTargetSetup::GetFramebuffers() const
    {
        return m_framebuffers;
    }
}
