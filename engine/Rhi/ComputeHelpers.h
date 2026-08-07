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
     * @param frame_index Frame-in-flight index for per-frame descriptor set
     * selection. Callers maintain their own counter (modulo the binding's
     * back-buffer count).
     */
    void BindComputeResource(
        vk::CommandBuffer cb, ComputeStage &stage, ComputeResourceBinding &binding, uint32_t frame_index
    );

    /**
     * @brief Issue a compute dispatch.
     */
    void DispatchCompute(vk::CommandBuffer cb, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z);
} // namespace Engine::Rhi

#endif // ENGINE_RHI_COMPUTEHELPERS_INCLUDED
