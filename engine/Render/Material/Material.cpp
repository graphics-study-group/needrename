#include "Material.h"
#include "Render/Pipeline/Pipeline.h"

namespace Engine {
    std::vector<vk::DescriptorSetLayout> Material::GetGlobalDescriptorSetLayout() {
        const auto & pool = m_system.lock()->GetGlobalConstantDescriptorPool();
        return {pool.GetPerSceneConstantLayout().get(), pool.GetPerCameraConstantLayout().get()};
    }
    Material::Material (std::weak_ptr<RenderSystem> system) 
    : m_system(system) {
    }
    const Pipeline *Material::GetSkinnedPipeline(uint32_t pass_index)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, 
            "A material does not have pipeline for skinned meshes, using non-skinned as fallback."
        );
        return this->GetPipeline(pass_index);
    }
    const PipelineLayout *Material::GetPipelineLayout(uint32_t pass_index) const
    {
        assert(pass_index < m_passes.size());
        return m_passes[pass_index].pipeline_layout.get();
    }
    const PipelineLayout *Material::GetSkinnedPipelineLayout(uint32_t pass_index) const
    {
        assert(pass_index < m_passes.size());
        if (!m_passes[pass_index].skinned_pipeline_layout) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, 
                "Requesting skinned pipeline layout for a material without skinned pipeline, using non-skinned as fallback."
            );
            return m_passes[pass_index].pipeline_layout.get();
        }
        return m_passes[pass_index].skinned_pipeline_layout.get();
    }
    vk::DescriptorSet Material::GetDescriptorSet(uint32_t pass_index) const
    {
        assert(pass_index < m_passes.size());
        return m_passes[pass_index].descriptor_set;
    }
    void Material::WriteDescriptors() const
    {
    }
};
