#include "Rhi/Command/CommandBuffer.h"

#include "Rhi/Backend/Vulkan/VulkanDebugUtils.h"
#include "Rhi/Backend/Vulkan/VulkanHandles.h"
#include "Rhi/Backend/Vulkan/VulkanStructs.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Device/DeviceInterface.h"

#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    struct CommandBuffer::impl {
        vk::UniqueCommandBuffer buffer{};
        vk::Device device{};
    };

    CommandBuffer::CommandBuffer(std::unique_ptr<impl> p) noexcept : pimpl(std::move(p)) {}

    std::vector<CommandBuffer> CommandBuffer::Create(
        const DeviceContext &device_context, CommandBufferPool pool, uint32_t count
    ) {
        const DeviceInterface &device_interface = device_context.GetDeviceInterface();
        const vk::Device device = ToVulkanDevice(device_interface.GetDevice());
        const QueueInfo &queue_info = device_interface.GetQueueInfo();

        const vk::CommandPool command_pool = pool == CommandBufferPool::GraphicsOneTime
                                                 ? queue_info.graphicsOneTimePool.get()
                                                 : queue_info.graphicsPool.get();
        auto allocated = device.allocateCommandBuffersUnique(
            vk::CommandBufferAllocateInfo{command_pool, vk::CommandBufferLevel::ePrimary, count}
        );

        std::vector<CommandBuffer> buffers;
        buffers.reserve(count);
        for (auto &unique : allocated) {
            auto p = std::make_unique<impl>();
            p->device = device;
            p->buffer = std::move(unique);
            buffers.push_back(CommandBuffer(std::move(p)));
        }
        return buffers;
    }

    CommandBuffer::~CommandBuffer() = default;
    CommandBuffer::CommandBuffer(CommandBuffer &&) noexcept = default;
    CommandBuffer &CommandBuffer::operator=(CommandBuffer &&) noexcept = default;

    CommandBufferHandle CommandBuffer::GetHandle() const noexcept {
        return FromVulkan(pimpl->buffer.get());
    }

    void CommandBuffer::Begin() {
        pimpl->buffer->begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    }

    void CommandBuffer::End() {
        pimpl->buffer->end();
    }

    void CommandBuffer::Reset() {
        pimpl->buffer->reset();
    }
} // namespace Engine::Rhi

