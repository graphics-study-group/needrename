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

    /// @brief A synchronization class with only three synchronization primitives:
    /// - a semaphore signaling ready to draw
    /// - a semaphore signaling finished
    /// - a fence for previous frame
    class TwoStageSynchronization : public Synchronization
    {
    public:

        TwoStageSynchronization(const RenderSystem & system);
        virtual ~TwoStageSynchronization() = default;

        virtual vk::Semaphore GetNextImageSemaphore(uint32_t frame) const;
        virtual std::vector<vk::Semaphore> GetCommandBufferWaitSignals(uint32_t frame) const;
        virtual std::vector<vk::PipelineStageFlags> GetCommandBufferWaitSignalFlags(uint32_t frame) const;
        virtual std::vector<vk::Semaphore> GetCommandBufferSigningSignals(uint32_t frame) const;
        virtual vk::Fence GetCommandBufferFence(uint32_t frame) const;

    protected:
        vk::UniqueSemaphore m_imageAvailable {};
        vk::UniqueSemaphore m_renderFinished {};
        vk::UniqueFence m_inflight {};
    };

    class InFlightTwoStageSynchronization : public Synchronization
    {
    public:

        InFlightTwoStageSynchronization(const RenderSystem & system);
        virtual ~InFlightTwoStageSynchronization() = default;

        virtual vk::Semaphore GetNextImageSemaphore(uint32_t inflight) const;
        virtual std::vector<vk::Semaphore> GetCommandBufferWaitSignals(uint32_t inflight) const;
        virtual std::vector<vk::PipelineStageFlags> GetCommandBufferWaitSignalFlags(uint32_t inflight) const;
        virtual std::vector<vk::Semaphore> GetCommandBufferSigningSignals(uint32_t inflight) const;
        virtual vk::Fence GetCommandBufferFence(uint32_t inflight) const;

    protected:
        static constexpr uint32_t INFLIGHT_SIZE = 3;
        std::array<vk::UniqueSemaphore, INFLIGHT_SIZE> m_imageAvailable {};
        std::array<vk::UniqueSemaphore, INFLIGHT_SIZE> m_renderFinished {};
        std::array<vk::UniqueFence, INFLIGHT_SIZE> m_inflight {};
    };
} // namespace Engine


#endif // RENDER_PIPELINE_SYNCHRONIZATION_INCLUDED
