#include "Material.h"
#include "Render/Pipeline/Pipeline.h"

namespace Engine {
    std::vector<vk::DescriptorSetLayout> Material::GetGlobalDescriptorSetLayout() {
        const auto & pool = m_renderSystem.lock()->GetGlobalConstantDescriptorPool();
        return {pool.GetPerCameraConstantLayout().get()};
    }

    Material::Material (std::weak_ptr<RenderSystem> system) 
    : m_renderSystem(system) {
    }
    const Pipeline& Material::GetPipeline(uint32_t pass_index) const {
        auto itr = m_pipelines.find(pass_index);
        assert(itr != m_pipelines.end());
        return *(itr->second.first);
    }
    const PipelineLayout& Material::GetPipelineLayout(uint32_t pass_index) const {
        auto itr = m_pipelines.find(pass_index);
        assert(itr != m_pipelines.end());
        return *(itr->second.second);
    }
};
