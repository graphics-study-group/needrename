#ifndef PIPELINE_COMMANDBUFFER_BUFFERTRANSFERHELPER_INCLUDED
#define PIPELINE_COMMANDBUFFER_BUFFERTRANSFERHELPER_INCLUDED

#include <vulkan/vulkan.hpp>

namespace vk {
    class MemoryBarrier2;
}

namespace Engine {
    class BufferTransferHelper {
    public:
        enum class BufferTransferType {
            // Barrier before writing to a vertex buffer.
            VertexBefore,
            // Barrier after writing to a vertex buffer.
            VertexAfter,
            // Barrier before writing to a read-only storage buffer.
            ReadonlyStorageBefore,
            // Barrier after writing to a read-only storage buffer.
            ReadonlyStorageAfter,
            // Barrier between clear (e.g. `vkCmdFillBuffer`) and copy (e.g. `vkCmdCopyBufferToImage`) commands.
            ClearAfter
        };
        
        static vk::MemoryBarrier2 GetBufferBarrier(BufferTransferType type);
    protected:
        static std::pair<vk::PipelineStageFlagBits2, vk::AccessFlags2> GetScope1(BufferTransferType type);
        static std::pair<vk::PipelineStageFlagBits2, vk::AccessFlags2> GetScope2(BufferTransferType type);
    };
}

#endif // PIPELINE_COMMANDBUFFER_BUFFERTRANSFERHELPER_INCLUDED
