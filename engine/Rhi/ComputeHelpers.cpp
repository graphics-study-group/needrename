#include "ComputeHelpers.h"

#include "ComputeResourceBinding.h"
#include "ComputeStage.h"

#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    void BindComputeStage(vk::CommandBuffer cb, ComputeStage &stage) {
        cb.bindPipeline(vk::PipelineBindPoint::eCompute, stage.GetPipeline());
    }

    void BindComputeResource(
        vk::CommandBuffer cb, ComputeStage &stage, ComputeResourceBinding &binding, uint32_t slot
    ) {
        auto offsets = binding.UpdateGPUInfo(slot);
        cb.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute, stage.GetPipelineLayout(), 0, {binding.GetDescriptorSet(slot)}, offsets
        );
    }

    void DispatchCompute(vk::CommandBuffer cb, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) {
        cb.dispatch(group_count_x, group_count_y, group_count_z);
    }
} // namespace Engine::Rhi
