#include "RenderPass.h"

#include "Render/Pipeline/Framebuffers.h"

namespace Engine
{
    RenderPass::RenderPass(std::weak_ptr<RenderSystem> system) : VkWrapper(system), m_framebuffers(system) {}

    void RenderPass::CreateRenderPass() {

        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating render pass.");

        vk::RenderPassCreateInfo info{};
        info.setAttachmentCount(m_attachments.size())
            .setPAttachments(m_attachments.data());
        info.setSubpassCount(m_subpasses.size()).setPSubpasses(m_subpasses.data());
        info.setDependencyCount(m_dependencies.size());
        info.setPDependencies(m_dependencies.empty() ? nullptr : m_dependencies.data());
        m_handle = m_system.lock()->getDevice().createRenderPassUnique(info);
    }

    void RenderPass::CreateFramebuffers() {
        Framebuffers fb{m_system};
        fb.CreateFramebuffers(*this);
        m_framebuffers = std::move(fb);
    }

    Subpass RenderPass::GetSubpass(
        uint32_t index) {
        assert(m_handle && "Getting subpass from an empty render pass.");
        return {m_handle.get(), index};
    }

    RenderPass& RenderPass::SetAttachments(std::vector<vk::AttachmentDescription> attachments) {
        m_attachments = attachments;
        return *this;
    }

    RenderPass& RenderPass::SetSubpasses(std::vector<vk::SubpassDescription> subpasses) {
        m_subpasses = subpasses;
        return *this;
    }

    RenderPass& RenderPass::SetDependencies(std::vector<vk::SubpassDependency> dependencies) {
        m_dependencies = dependencies;
        return *this;
    }

    const std::vector<vk::AttachmentDescription>& RenderPass::GetAttachments() const {
        return m_attachments;
    }

    const Framebuffers& RenderPass::GetFramebuffers() const {
        return m_framebuffers;
    }

} // namespace Engine

