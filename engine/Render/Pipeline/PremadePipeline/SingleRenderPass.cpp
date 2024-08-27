#include "SingleRenderPass.h"

namespace Engine
{
    SingleRenderPass::SingleRenderPass(std::weak_ptr<RenderSystem> system) : RenderPass(system) {
        CreateRenderPass();
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

        vk::SubpassDependency dep{};
        dep.srcSubpass = vk::SubpassExternal;
        dep.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dep.srcAccessMask = vk::AccessFlagBits::eNone;
        dep.dstSubpass = 0;
        dep.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dep.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

        SetAttachments({att}).SetSubpasses({subp}).SetDependencies({dep});
        RenderPass::CreateRenderPass();
    }
} // namespace Engine

