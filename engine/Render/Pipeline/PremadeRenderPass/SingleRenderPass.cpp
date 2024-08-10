#include "SingleRenderPass.h"

namespace Engine
{
    SingleRenderPass::SingleRenderPass(std::weak_ptr<RenderSystem> system) : RenderPass(system) {
    }

    void SingleRenderPass::CreateRenderPass() {
        vk::AttachmentDescription att{};
        att.format = m_system.lock()->getSwapchainInfo().format.format;
        att.samples = vk::SampleCountFlagBits::e1;
        att.loadOp = vk::AttachmentLoadOp::eClear;
        att.storeOp = vk::AttachmentStoreOp::eStore;
        att.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        att.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        att.initialLayout = vk::ImageLayout::eUndefined;
        att.finalLayout = vk::ImageLayout::ePresentSrcKHR;

        vk::AttachmentReference ref{};
        ref.attachment = 0;
        ref.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::SubpassDescription subp{};
        subp.colorAttachmentCount = 1;
        subp.pColorAttachments = &ref;

        RenderPass::CreateRenderPass({att}, {subp}, {});
    }
} // namespace Engine

