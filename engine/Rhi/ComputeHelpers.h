#ifndef ENGINE_RHI_COMPUTEHELPERS_INCLUDED
#define ENGINE_RHI_COMPUTEHELPERS_INCLUDED

#include <cstdint>

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
     */
    void BindComputeResource(vk::CommandBuffer cb, ComputeStage &stage, ComputeResourceBinding &binding, uint32_t slot);

    /**
     * @brief Issue a compute dispatch.
     */
    void DispatchCompute(vk::CommandBuffer cb, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z);
} // namespace Engine::Rhi

#endif // ENGINE_RHI_COMPUTEHELPERS_INCLUDED
