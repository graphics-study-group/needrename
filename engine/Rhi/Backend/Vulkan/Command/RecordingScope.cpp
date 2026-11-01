#include "Rhi/Command/RecordingScope.h"

#include "Rhi/Backend/Vulkan/VulkanHandles.h"
#include "Rhi/Backend/Vulkan/VulkanStructs.h"
#include "Rhi/Command/CommandEncoder.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Device/DeviceInterface.h"

#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    struct RecordingScope::impl {
        DeviceContext *context{nullptr};
        vk::UniqueCommandBuffer buffer{};
        std::unique_ptr<CommandEncoder> encoder{};
        bool submitted{false};
    };

    RecordingScope::RecordingScope(DeviceContext &context) : pimpl(std::make_unique<impl>()) {
        pimpl->context = &context;

        const auto &queue_info = context.GetDeviceInterface().GetQueueInfo();
        const vk::Device device = ToVulkanDevice(context.GetDeviceInterface().GetDevice());

        auto buffers = device.allocateCommandBuffersUnique(
            vk::CommandBufferAllocateInfo{
                queue_info.graphicsOneTimePool.get(), vk::CommandBufferLevel::ePrimary, 1
            }
        );
        pimpl->buffer = std::move(buffers[0]);
        pimpl->buffer->begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        pimpl->encoder = std::make_unique<CommandEncoder>(FromVulkan(static_cast<VkCommandBuffer>(pimpl->buffer.get())));
    }

    RecordingScope::~RecordingScope() {
        if (!pimpl->submitted && pimpl->buffer) {
            // An abandoned recording is closed rather than left open: a command
            // buffer that stays in the recording state cannot be returned to the
            // pool, and the application would leak one per abandoned scope.
            pimpl->buffer->end();
        }
    }

    CommandEncoder &RecordingScope::Encoder() noexcept {
        return *pimpl->encoder;
    }

    void RecordingScope::Submit() {
        if (pimpl->submitted) {
            return;
        }
        pimpl->submitted = true;

        pimpl->buffer->end();

        const auto &queue_info = pimpl->context->GetDeviceInterface().GetQueueInfo();
        const vk::Device device = ToVulkanDevice(pimpl->context->GetDeviceInterface().GetDevice());

        vk::UniqueFence fence = device.createFenceUnique(vk::FenceCreateInfo{});
        const vk::CommandBufferSubmitInfo buffer_info{pimpl->buffer.get()};
        queue_info.graphicsQueue.submit2(vk::SubmitInfo2{vk::SubmitFlags{}, {}, {buffer_info}, {}}, fence.get());
        (void)device.waitForFences({fence.get()}, VK_TRUE, UINT64_MAX);
    }
} // namespace Engine::Rhi
