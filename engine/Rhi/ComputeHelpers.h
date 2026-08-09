#ifndef ENGINE_RHI_COMPUTEHELPERS_INCLUDED
#define ENGINE_RHI_COMPUTEHELPERS_INCLUDED

#include <cassert>
#include <cstdint>

#include <vulkan/vulkan.hpp>

#include "Rhi/ComputeStage.h"

namespace vk {
    class CommandBuffer;
}

namespace Engine::Rhi {
    class ComputeStage;
    class ComputeResourceBinding;

    /**
     * @brief Bind a compute pipeline to the given command buffer.
     */
    void BindComputeStage(vk::CommandBuffer cb, ComputeStage &stage);

    /**
     * @brief Bind the resources of a compute stage.
     *
     * @param slot Rotation slot index for descriptor-set / UBO-slice
     * selection. Callers advance the slot in lockstep with their own
     * submission cadence, modulo the binding's declared slot count.
     * Defaults to 0 for the common no-rotation case.
     */
    void BindComputeResource(
        vk::CommandBuffer cb, ComputeStage &stage, ComputeResourceBinding &binding, uint32_t slot = 0
    );

    /**
     * @brief Record push constants for a compute stage.
     *
     * The value's layout must match the shader's push constant block.
     * A debug assertion guards against sizes beyond the reflected block.
     */
    template <typename T>
    void PushConstants(vk::CommandBuffer cb, ComputeStage &stage, const T &value) {
        static_assert(sizeof(T) % 4 == 0, "Push constant size must be a multiple of 4 bytes.");
        assert(sizeof(T) <= stage.GetPushConstantSize());
        cb.pushConstants(stage.GetPipelineLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(T), &value);
    }

    /**
     * @brief Issue a compute dispatch.
     */
    void DispatchCompute(vk::CommandBuffer cb, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z);
} // namespace Engine::Rhi

#endif // ENGINE_RHI_COMPUTEHELPERS_INCLUDED
