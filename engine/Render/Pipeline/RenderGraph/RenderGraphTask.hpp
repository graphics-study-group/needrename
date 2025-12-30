#ifndef PIPELINE_RENDERGRAPH_RENDERGRAPHTASK
#define PIPELINE_RENDERGRAPH_RENDERGRAPHTASK

#include <variant>
#include <vector>

namespace vk {
    struct CommandBuffer;
    struct MemoryBarrier2;
    struct BufferMemoryBarrier2;
    struct ImageMemoryBarrier2;
}

namespace Engine {
    class RenderTargetTexture;

    namespace RenderGraphImpl {
        struct Command {
            enum class Type {
                Graphics,
                Compute,
                Transfer
            } type;

            std::function <void(vk::CommandBuffer)> func;
        };

        struct Synchronization {
            std::vector <vk::MemoryBarrier2> memory_barriers;
            std::vector <vk::BufferMemoryBarrier2> buffer_barriers;
            std::vector <vk::ImageMemoryBarrier2> image_barriers;
        };

        struct Present {
            RenderTargetTexture & present_from;
            uint32_t extent_x, extent_y;
            int32_t offset_x, offset_y;
        };

        typedef std::variant <Command, Synchronization, Present> Task;
    }
}

#endif // PIPELINE_RENDERGRAPH_RENDERGRAPHTASK
