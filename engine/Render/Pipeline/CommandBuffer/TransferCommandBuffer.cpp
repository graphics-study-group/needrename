#include "TransferCommandBuffer.h"

namespace Engine
{
    TransferCommandBuffer::TransferCommandBuffer(
        RenderSystem &system, 
        vk::CommandBuffer cb
    ) : ICommandBuffer(cb), m_system(system)
    {
    }

} // namespace Engine

