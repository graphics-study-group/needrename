#include "ComputeCommandBuffer.h"

#include "Render/Pipeline/Compute/ComputeStage.h"

namespace Engine {
    ComputeCommandBuffer::ComputeCommandBuffer(RenderSystem &system_, vk::CommandBuffer cb, uint32_t frame_in_flight) :
        ICommandBuffer(cb), system(system_), inflight_frame_index(frame_in_flight) {
    }
    void ComputeCommandBuffer::BindComputeStage(ComputeStage &stage) {
        this->m_bound_pipeline = stage;
        this->cb.bindPipeline(vk::PipelineBindPoint::eCompute, stage.GetPipeline());
    }
    void ComputeCommandBuffer::DispatchCompute(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
        assert(this->m_bound_pipeline.has_value() && "Compute pipeline is not bound.");
        const auto &stage = this->m_bound_pipeline.value();

        stage.get().WriteUBO();
        stage.get().WriteDescriptorSet();

        this->cb.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute, stage.get().GetPipelineLayout(), 0, {stage.get().GetDescriptorSet()}, {}
        );
        this->cb.dispatch(groupCountX, groupCountY, groupCountZ);
    }
} // namespace Engine
