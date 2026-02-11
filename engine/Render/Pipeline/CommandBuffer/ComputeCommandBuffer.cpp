#include "ComputeCommandBuffer.h"

#include "Render/Pipeline/Compute/ComputeStage.h"
#include "Render/Pipeline/Compute/ComputeResourceBinding.h"

namespace Engine {
    
    ComputeCommandBuffer::ComputeCommandBuffer(vk::CommandBuffer cb, uint32_t frame_in_flight) :
        ICommandBuffer(cb), inflight_frame_index(frame_in_flight) {
    }

    void ComputeCommandBuffer::BindComputeStage(ComputeStage &stage) {
        bound_pipeline = stage;
        this->cb.bindPipeline(vk::PipelineBindPoint::eCompute, stage.GetPipeline());
    }
    void ComputeCommandBuffer::BindComputeResource(ComputeResourceBinding &binding) {
        assert(bound_pipeline.has_value() && "Compute pipeline is not bound.");

        auto offsets = binding.UpdateGPUInfo(inflight_frame_index);
        this->cb.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute,
            bound_pipeline.value().get().GetPipelineLayout(),
            0,
            {binding.GetDescriptorSet(inflight_frame_index)},
            offsets
        );
    }
    void ComputeCommandBuffer::DispatchCompute(
        uint32_t groupCountX,
        uint32_t groupCountY,
        uint32_t groupCountZ
    ) {
        assert(bound_pipeline.has_value() && "Compute pipeline is not bound.");
        const auto &stage = bound_pipeline.value();
        this->cb.dispatch(groupCountX, groupCountY, groupCountZ);
    }
} // namespace Engine
