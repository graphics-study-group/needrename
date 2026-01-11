#ifndef PIPELINE_RENDERGRAPH_RENDERGRAPHTASK
#define PIPELINE_RENDERGRAPH_RENDERGRAPHTASK

#include <variant>
#include <vector>
#include <functional>

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

            bool empty() const noexcept {
                return memory_barriers.empty() && buffer_barriers.empty() && image_barriers.empty();
            }

            void clear() noexcept {
                memory_barriers.clear();
                buffer_barriers.clear();
                image_barriers.clear();
            }

            /**
             * @brief Get a function object that records barriers into a command buffer.
             * 
             * Moves from saved barriers.
             */
            std::function <void(vk::CommandBuffer)> GetBarrierCommand() {
                return [
                    mb = std::move(memory_barriers),
                    bb = std::move(buffer_barriers),
                    ib = std::move(image_barriers)
                ](vk::CommandBuffer cb) -> void {
                    cb.pipelineBarrier2(vk::DependencyInfo{
                        vk::DependencyFlags{},
                        mb, bb, ib
                    });
                };
            }

            /**
             * @brief Get a function object that records barriers into a command buffer.
             */
            std::function <void(vk::CommandBuffer)> GetBarrierCommandCopy() {
                return [
                    mb = memory_barriers,
                    bb = buffer_barriers,
                    ib = image_barriers
                ](vk::CommandBuffer cb) -> void {
                    cb.pipelineBarrier2(vk::DependencyInfo{
                        vk::DependencyFlags{},
                        mb, bb, ib
                    });
                };
            }
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
