#include "SingleRenderPassWithDepth.h"

namespace Engine {
    SingleRenderPassWithDepth::SingleRenderPassWithDepth(std::weak_ptr<RenderSystem> system) : RenderPass(system) {
    }

    void SingleRenderPassWithDepth::CreateRenderPass() {
        const auto & swapchain = m_system.lock()->GetSwapchain();
        if (!swapchain.IsDepthEnabled()) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, 
                "The render pass has depth support, but the swap chain is not depth test enabled."
            );
        }

        vk::AttachmentDescription color_attachment{};
        color_attachment.format = swapchain.GetImageFormat().format;
        color_attachment.samples = vk::SampleCountFlagBits::e1;
        color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
        color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
        color_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        color_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        color_attachment.initialLayout = vk::ImageLayout::eUndefined;
        color_attachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

        vk::AttachmentDescription depth_attachment{};
        depth_attachment.format = swapchain.DEPTH_FORMAT;
        depth_attachment.samples = vk::SampleCountFlagBits::e1;
        depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
        depth_attachment.storeOp = vk::AttachmentStoreOp::eDontCare;
        depth_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        depth_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        depth_attachment.initialLayout = vk::ImageLayout::eUndefined;
        depth_attachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::AttachmentReference color_ref{0, vk::ImageLayout::eColorAttachmentOptimal};
        vk::AttachmentReference depth_ref{1, vk::ImageLayout::eDepthStencilAttachmentOptimal};

        vk::SubpassDescription subp{};
        subp.colorAttachmentCount = 1;
        subp.pColorAttachments = &color_ref;
        subp.pDepthStencilAttachment = &depth_ref;

        vk::SubpassDependency color_dependency{};
        color_dependency.srcSubpass = vk::SubpassExternal;
        color_dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        color_dependency.srcAccessMask = vk::AccessFlagBits::eNone;
        color_dependency.dstSubpass = 0;
        color_dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        color_dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

        vk::SubpassDependency depth_dependency{};
        depth_dependency.srcSubpass = vk::SubpassExternal;
        depth_dependency.srcStageMask = vk::PipelineStageFlagBits::eLateFragmentTests;
        depth_dependency.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        depth_dependency.dstSubpass = 0;
        depth_dependency.dstStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests;
        depth_dependency.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;

        SetAttachments({color_attachment, depth_attachment});
        SetSubpasses({subp});
        SetDependencies({color_dependency, depth_dependency});
        RenderPass::CreateRenderPass();
    }
};
