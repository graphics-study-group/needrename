#include "Material.h"
#include "Render/Pipeline/Pipeline.h"

namespace Engine {
    std::vector<vk::DescriptorSetLayout> Material::GetGlobalDescriptorSetLayout() {
        const auto & pool = m_system.lock()->GetGlobalConstantDescriptorPool();
        return {pool.GetPerCameraConstantLayout().get()};
    }
    Material::Material (std::weak_ptr<RenderSystem> system) 
    : m_system(system) {
    }
    const PipelineLayout * Material::GetPipelineLayout(uint32_t pass_index) const {
        assert(pass_index < m_passes.size());
        return m_passes[pass_index].pipeline_layout.get();
    }
    vk::DescriptorSet Material::GetDescriptorSet(uint32_t pass_index) const {
        assert(pass_index < m_passes.size());
        return m_passes[pass_index].descriptor_set;
    }
    void Material::WriteDescriptors() const
    {
    }
};
