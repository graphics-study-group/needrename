#ifndef PIPELINE_COMMANDBUFFER_COMPUTECOMMANDBUFFER_INCLUDED
#define PIPELINE_COMMANDBUFFER_COMPUTECOMMANDBUFFER_INCLUDED

#include "Render/Pipeline/CommandBuffer/ICommandBuffer.h"
#include <vulkan/vulkan.hpp>
#include <any>

namespace Engine {
    class RenderSystem;
    class ComputeCommandBuffer : public ICommandBuffer {
    public:
        ComputeCommandBuffer(RenderSystem & system,
            vk::CommandBuffer cb,
            uint32_t frame_in_flight);

        void BindComputePipeline(vk::Pipeline, vk::PipelineLayout);

        void SetComputeParam(uint32_t key, std::any value);

        void DispatchCompute(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);

    private:
        RenderSystem & system;
        uint32_t inflight_frame_index;
    };
}

#endif // PIPELINE_COMMANDBUFFER_COMPUTECOMMANDBUFFER_INCLUDED
