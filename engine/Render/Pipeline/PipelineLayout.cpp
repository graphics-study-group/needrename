#include "PipelineLayout.h"

#include "Render/RenderSystem.h"
#include "Render/ConstantData/PerModelConstants.h"

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

    void PipelineLayout::CreateWithDefault(const std::vector<vk::DescriptorSetLayout> & extra_descriptor_set)
    {
        auto system = m_system.lock();
        std::array <vk::PushConstantRange, 1> default_push_constant{ConstantData::PerModelConstantPushConstant::GetPushConstantRange()};
        std::vector <vk::DescriptorSetLayout> default_set_layout;
        default_set_layout.push_back(system->GetGlobalConstantDescriptorPool().GetPerCameraConstantLayout().get());
        default_set_layout.insert(default_set_layout.end(), extra_descriptor_set.begin(), extra_descriptor_set.end());

        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating pipeline layout.");
        vk::PipelineLayoutCreateInfo info{
            vk::PipelineLayoutCreateFlags{0},
            default_set_layout,
            default_push_constant
        };
        m_handle = system->getDevice().createPipelineLayoutUnique(info);
    }
} // namespace Engine
