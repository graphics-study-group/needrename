#include "ComputeHelpers.h"

#include "ComputeResourceBinding.h"
#include "ComputeStage.h"

#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    void BindComputeStage(vk::CommandBuffer cb, ComputeStage &stage) {
        cb.bindPipeline(vk::PipelineBindPoint::eCompute, stage.GetPipeline());
    }

    void BindComputeResource(
        vk::CommandBuffer cb, ComputeStage &stage, ComputeResourceBinding &binding, uint32_t frame_index
    ) {
        auto offsets = binding.UpdateGPUInfo(frame_index);
        cb.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute,
            stage.GetPipelineLayout(),
            0,
            {binding.GetDescriptorSet(frame_index)},
            offsets
        );
    }

    void DispatchCompute(vk::CommandBuffer cb, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) {
        cb.dispatch(group_count_x, group_count_y, group_count_z);
    }
} // namespace Engine::Rhi
