#include "ComputeCommandBuffer.h"

namespace Engine {
    ComputeCommandBuffer::ComputeCommandBuffer(
        RenderSystem &system_, 
        vk::CommandBuffer cb, 
        uint32_t frame_in_flight
    ) : ICommandBuffer(cb), system(system_), inflight_frame_index(frame_in_flight)
    {
    }
}
