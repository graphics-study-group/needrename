#include "RenderPass.h"

namespace Engine
{
    RenderPass::RenderPass(std::weak_ptr<RenderSystem> system) : VkWrapper(system) {}

    void RenderPass::CreateRenderPass(
        std::vector<vk::AttachmentDescription> attachments,
        std::vector<vk::SubpassDescription> subpasses,
        std::vector<vk::SubpassDependency> dependencies) {

        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating render pass.");

        vk::RenderPassCreateInfo info{};
        info.setAttachmentCount(attachments.size())
            .setPAttachments(attachments.data());
        info.setSubpassCount(subpasses.size()).setPSubpasses(subpasses.data());
        info.setDependencyCount(dependencies.size());
        info.setPDependencies(dependencies.empty() ? nullptr : dependencies.data());
        m_handle = m_system.lock()->getDevice().createRenderPassUnique(info);
    }

    Subpass RenderPass::GetSubpass(
        uint32_t index) {
        assert(m_handle && "Getting subpass from an empty render pass.");
        return {m_handle.get(), index};
    }

} // namespace Engine

