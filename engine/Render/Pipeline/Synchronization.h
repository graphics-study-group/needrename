#ifndef RENDER_PIPELINE_SYNCHRONIZATION_INCLUDED
#define RENDER_PIPELINE_SYNCHRONIZATION_INCLUDED

#include <vulkan/vulkan.hpp>
#include <memory>

namespace Engine
{
    class RenderSystem;

    /// @brief 
    class Synchronization
    {
    public:
        Synchronization(const RenderSystem & system);
        virtual ~Synchronization() = default;

        virtual vk::Semaphore GetNextImageSemaphore() const = 0;
        virtual std::vector<vk::Semaphore> GetCommandBufferWaitSignals(uint32_t stage) const = 0;
        virtual std::vector<vk::PipelineStageFlags> GetCommandBufferWaitSignalFlags(uint32_t stage) const = 0;
        virtual std::vector<vk::Semaphore> GetCommandBufferSigningSignals(uint32_t stage) const = 0;
        virtual vk::Fence GetCommandBufferFence(uint32_t stage) const = 0;
    
    protected:
        const RenderSystem & m_system;
    };

    /// @brief A simple two stage synchronization consists of two semaphores and one fence
    class TwoStageSynchronization : public Synchronization
    {
    public:

        TwoStageSynchronization(const RenderSystem & system);
        virtual ~TwoStageSynchronization() = default;

        virtual vk::Semaphore GetNextImageSemaphore() const;
        virtual std::vector<vk::Semaphore> GetCommandBufferWaitSignals(uint32_t stage) const;
        virtual std::vector<vk::PipelineStageFlags> GetCommandBufferWaitSignalFlags(uint32_t stage) const;
        virtual std::vector<vk::Semaphore> GetCommandBufferSigningSignals(uint32_t stage) const;
        virtual vk::Fence GetCommandBufferFence(uint32_t stage) const;

    protected:
        vk::UniqueSemaphore m_imageAvailable {};
        vk::UniqueSemaphore m_renderFinished {};
        vk::UniqueFence m_inflight {};
    };
} // namespace Engine


#endif // RENDER_PIPELINE_SYNCHRONIZATION_INCLUDED
