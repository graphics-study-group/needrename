#ifndef PIPELINE_COMMANDBUFFER_BUFFERTRANSFERHELPER_INCLUDED
#define PIPELINE_COMMANDBUFFER_BUFFERTRANSFERHELPER_INCLUDED

#include <vulkan/vulkan.hpp>

namespace Engine {
    class BufferTransferHelper {
    public:
        enum class BufferTransferType {
            VertexBefore,
            VertexAfter,
            ReadonlyStorageBefore,
            ReadonlyStorageAfter,
        };
        
        static vk::MemoryBarrier2 GetBufferBarrier(BufferTransferType type);
    protected:
        static std::pair<vk::PipelineStageFlagBits2, vk::AccessFlags2> GetScope1(BufferTransferType type);
        static std::pair<vk::PipelineStageFlagBits2, vk::AccessFlags2> GetScope2(BufferTransferType type);
    };
}

#endif // PIPELINE_COMMANDBUFFER_BUFFERTRANSFERHELPER_INCLUDED
