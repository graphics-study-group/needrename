#ifndef RENDER_PIPELINE_SYNCHRONIZATION_INCLUDED
#define RENDER_PIPELINE_SYNCHRONIZATION_INCLUDED

#include <vulkan/vulkan.hpp>
#include <memory>

namespace Engine
{
    class RenderSystem;

    /// @brief A synchronization class for command buffers.
    class Synchronization
    {
    public:
        Synchronization(const RenderSystem & system);
        virtual ~Synchronization() = default;

        virtual vk::Semaphore GetNextImageSemaphore(uint32_t frame) const = 0;
        virtual std::vector<vk::Semaphore> GetCommandBufferWaitSignals(uint32_t frame) const = 0;
        virtual std::vector<vk::PipelineStageFlags> GetCommandBufferWaitSignalFlags(uint32_t frame) const = 0;
        virtual std::vector<vk::Semaphore> GetCommandBufferSigningSignals(uint32_t frame) const = 0;
        virtual vk::Fence GetCommandBufferFence(uint32_t frame) const = 0;
    
    protected:
        const RenderSystem & m_system;
    };
} // namespace Engine


#endif // RENDER_PIPELINE_SYNCHRONIZATION_INCLUDED
