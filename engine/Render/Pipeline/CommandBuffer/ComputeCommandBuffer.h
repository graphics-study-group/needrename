#ifndef PIPELINE_COMMANDBUFFER_COMPUTECOMMANDBUFFER_INCLUDED
#define PIPELINE_COMMANDBUFFER_COMPUTECOMMANDBUFFER_INCLUDED

#include "Render/Pipeline/CommandBuffer/ICommandBuffer.h"
#include <any>

namespace Engine {
    class RenderSystem;
    class ComputeStage;
    class ComputeCommandBuffer : public ICommandBuffer {
    public:
        ComputeCommandBuffer(RenderSystem &system, vk::CommandBuffer cb, uint32_t frame_in_flight);

        void BindComputeStage(ComputeStage &stage);

        void DispatchCompute(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);

    private:
        RenderSystem &system;
        uint32_t inflight_frame_index;
        std::optional<std::reference_wrapper<ComputeStage>> m_bound_pipeline;
    };
} // namespace Engine

#endif // PIPELINE_COMMANDBUFFER_COMPUTECOMMANDBUFFER_INCLUDED
