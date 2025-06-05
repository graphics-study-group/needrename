#ifndef PIPELINE_COMPUTE_COMPUTESTAGE_INCLUDED
#define PIPELINE_COMPUTE_COMPUTESTAGE_INCLUDED

#include "Render/Pipeline/PipelineInfo.h"

namespace Engine {
    class RenderSystem;
    class AssetRef;

    class ComputeStage {
        using PassInfo = PipelineInfo::PassInfo;
        using InstancedPassInfo = PipelineInfo::InstancedPassInfo;

        RenderSystem & m_system;
        std::shared_ptr <AssetRef> m_asset;

        struct impl;
        std::unique_ptr <impl> pimpl;

        void CreatePipeline();
    public:
        ComputeStage(RenderSystem & system, std::shared_ptr <AssetRef> asset);
        ~ComputeStage();

        void SetInBlockVariable(uint32_t index, std::any var);
        void SetDescVariable(uint32_t index, std::any var);
        std::optional<std::pair<uint32_t, bool>> GetVariableIndex(const std::string & name) const noexcept;

        void WriteDescriptorSet();
        void WriteUBO();

        vk::Pipeline GetPipeline() const noexcept;
        vk::PipelineLayout GetPipelineLayout() const noexcept;
        vk::DescriptorSetLayout GetDescriptorSetLayout() const noexcept;
        vk::DescriptorSet GetDescriptorSet() const noexcept;
    };
}

#endif // PIPELINE_COMPUTE_COMPUTESTAGE_INCLUDED
