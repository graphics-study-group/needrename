#ifndef PIPELINE_COMMANDBUFFER_COMPUTECOMMANDBUFFER_INCLUDED
#define PIPELINE_COMMANDBUFFER_COMPUTECOMMANDBUFFER_INCLUDED

#include "Render/Pipeline/CommandBuffer/ICommandBuffer.h"

namespace Engine {
    class RenderSystem;
    class ComputeStage;
    class ComputeResourceBinding;

    class ComputeCommandBuffer : public ICommandBuffer {
    public:
        ComputeCommandBuffer(vk::CommandBuffer cb, uint32_t frame_in_flight);

        /**
         * @brief Bind a compute shader to the current command buffer.
         */
        void BindComputeStage(ComputeStage &stage);

        /**
         * @brief Bind compute resource to the current command buffer.
         * 
         * The binding should be created from the compute stage currently
         * bound to the command buffer, or the behavior is undefined.
         */
        void BindComputeResource(ComputeResourceBinding & binding);

        /**
         * @brief Dispatch a compute shader program.
         * 
         * The program must be bound through `BindComputeStage()`, and its
         * resource through `BindComputeResource()`.
         */
        void DispatchCompute(
            uint32_t groupCountX,
            uint32_t groupCountY,
            uint32_t groupCountZ
        );

    private:

        uint32_t inflight_frame_index;
        std::optional<std::reference_wrapper<ComputeStage>> bound_pipeline{std::nullopt};
    };
} // namespace Engine

#endif // PIPELINE_COMMANDBUFFER_COMPUTECOMMANDBUFFER_INCLUDED
