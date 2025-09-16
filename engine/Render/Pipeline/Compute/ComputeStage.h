#ifndef PIPELINE_COMPUTE_COMPUTESTAGE_INCLUDED
#define PIPELINE_COMPUTE_COMPUTESTAGE_INCLUDED

#include "Asset/InstantiatedFromAsset.h"
#include "Render/Pipeline/PipelineInfo.h"
#include <any>

namespace Engine {
    class RenderSystem;
    class ShaderAsset;

    class ComputeStage : public IInstantiatedFromAsset<ShaderAsset> {
        using PassInfo = PipelineInfo::PassInfo;

        RenderSystem &m_system;

        struct impl;
        std::unique_ptr<impl> pimpl;

    public:
        ComputeStage(RenderSystem &system);

        void Instantiate(const ShaderAsset &asset) override;

        ~ComputeStage();

        void SetInBlockVariable(uint32_t index, std::any var);
        void SetDescVariable(uint32_t index, std::any var);
        std::optional<std::pair<uint32_t, bool>> GetVariableIndex(const std::string &name) const noexcept;

        void WriteDescriptorSet();
        void WriteUBO();

        vk::Pipeline GetPipeline() const noexcept;
        vk::PipelineLayout GetPipelineLayout() const noexcept;
        vk::DescriptorSetLayout GetDescriptorSetLayout() const noexcept;
        vk::DescriptorSet GetDescriptorSet() const noexcept;
    };
} // namespace Engine

#endif // PIPELINE_COMPUTE_COMPUTESTAGE_INCLUDED
