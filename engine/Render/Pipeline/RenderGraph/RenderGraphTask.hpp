#ifndef PIPELINE_RENDERGRAPH_RENDERGRAPHTASK
#define PIPELINE_RENDERGRAPH_RENDERGRAPHTASK

#include <variant>
#include <vector>
#include <vulkan/vulkan.hpp>

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
            vk::Offset2D offset;
            vk::Extent2D extent;
        };

        typedef std::variant <Command, Synchronization, Present> Task;
    }
}

#endif // PIPELINE_RENDERGRAPH_RENDERGRAPHTASK
