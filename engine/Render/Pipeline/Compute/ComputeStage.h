#ifndef PIPELINE_COMPUTE_COMPUTESTAGE_INCLUDED
#define PIPELINE_COMPUTE_COMPUTESTAGE_INCLUDED

#include "Render/Pipeline/PipelineInfo.h"

namespace Engine {
    class RenderSystem;
    class AssetRef;

    class ComputeStage {
        using PassInfo = PipelineInfo::PassInfo;
        using InstancedPassInfo = PipelineInfo::InstancedPassInfo;

        PassInfo m_passInfo {};
        InstancedPassInfo m_instancedPassInfo {};

        void CreatePipeline();
    public:
        ComputeStage(std::weak_ptr <RenderSystem> system, std::shared_ptr <AssetRef> asset);
    };
}

#endif // PIPELINE_COMPUTE_COMPUTESTAGE_INCLUDED
