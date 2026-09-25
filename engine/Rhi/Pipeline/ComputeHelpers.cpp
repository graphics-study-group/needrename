#include "Rhi/Pipeline/ComputeHelpers.h"

#include "Rhi/Pipeline/ComputeResourceBinding.h"
#include "Rhi/Pipeline/ComputeStage.h"

#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    void BindComputeStage(vk::CommandBuffer cb, ComputeStage &stage) {
        cb.bindPipeline(vk::PipelineBindPoint::eCompute, stage.GetPipeline());
    }

    void BindComputeResource(
        vk::CommandBuffer cb, ComputeStage &stage, ComputeResourceBinding &binding, uint32_t slot
    ) {
        // Re-acquire on every use: the arena's recorded epoch must stay an upper
        // bound on the epochs whose command buffers reference the set.
        auto result = binding.UpdateGPUInfo(slot);
        cb.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute, stage.GetPipelineLayout(), 0, {result.set}, result.dynamic_offsets
        );
    }

    void DispatchCompute(vk::CommandBuffer cb, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) {
        cb.dispatch(group_count_x, group_count_y, group_count_z);
    }
} // namespace Engine::Rhi
