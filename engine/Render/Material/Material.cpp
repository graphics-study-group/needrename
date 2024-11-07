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
        assert(pass_index < m_passes.size());
        if (!m_passes[pass_index].skinned_pipeline) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, 
                "A material does not have pipeline for skinned meshes, returning non-skinned as fallback."
            );
            return m_passes[pass_index].pipeline.get();
        }
        return m_passes[pass_index].skinned_pipeline.get();
    }
    const PipelineLayout *Material::GetPipelineLayout(uint32_t pass_index)
    {
        assert(pass_index < m_passes.size());
        return m_passes[pass_index].pipeline_layout.get();
    }
    const PipelineLayout *Material::GetSkinnedPipelineLayout(uint32_t pass_index)
    {
        assert(pass_index < m_passes.size());
        if (!m_passes[pass_index].skinned_pipeline_layout) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, 
                "Requesting skinned pipeline layout for a material without skinned pipeline, constructing non-skinned as fallback."
            );
            m_passes[pass_index].skinned_pipeline_layout = std::make_unique <PipelineLayout> (m_system);
            m_passes[pass_index].skinned_pipeline_layout->CreateWithDefaultSkinned(this->GetMaterialDescriptorSetLayout(pass_index));
        }
        return m_passes[pass_index].skinned_pipeline_layout.get();
    }
    vk::DescriptorSet Material::GetDescriptorSet(uint32_t pass_index) const
    {
        assert(pass_index < m_passes.size());
        return m_passes[pass_index].material_descriptor_set;
    }
    vk::DescriptorSetLayout Material::GetMaterialDescriptorSetLayout(uint32_t pass_index) const
    {
        assert(pass_index < m_passes.size());
        return m_passes[pass_index].material_descriptor_set_layout;
    }
    void Material::WriteDescriptors() const
    {
    }
};
