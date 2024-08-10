#include "PipelineLayout.h"
#include "Render/RenderSystem.h"
#include <SDL3/SDL.h>

namespace Engine {
    PipelineLayout::PipelineLayout(std::weak_ptr<RenderSystem> system) : VkWrapper(system) {}
    void PipelineLayout::CreatePipelineLayout(
        const std::vector<vk::DescriptorSetLayout>& set,
        const std::vector<vk::PushConstantRange>& push) 
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating pipeline layout.");
        vk::PipelineLayoutCreateInfo info{};
        info.setLayoutCount = set.size();
        info.pSetLayouts = set.empty() ? nullptr : set.data(); 
        info.pushConstantRangeCount = push.size();
        info.pPushConstantRanges = push.empty() ? nullptr : push.data();
        m_handle = m_system.lock()->getDevice().createPipelineLayoutUnique(info);
    }
}  // namespace Engine
