#include "OffscreenPresentProvider.h"

#include "Render/Resource/ImageToBufferCopy.hpp"
#include "Render/Resource/RenderTargetTexture.h"
#include "Rhi/Buffer/DeviceBuffer.h"
#include "Rhi/Device/AllocatorState.h"
#include "Rhi/Device/DeviceInterface.h"
#include "Rhi/Device/MemoryTypes.h"

namespace Engine::RenderSystemState {
    struct OffscreenPresentProvider::impl {
        const Rhi::DeviceInterface *m_device_interface;
        const Rhi::AllocatorState *m_allocator;
        vk::Extent2D m_extent;
        vk::Format m_color_format;
        uint32_t m_image_count;
        uint32_t m_frame_counter = 0;

        // Host-visible present targets; lazily allocated on first PrepareCopy.
        std::vector<std::unique_ptr<Rhi::DeviceBuffer>> m_targets{};

        // One copy command buffer per present image, reused across frames.
        std::vector<vk::UniqueCommandBuffer> m_copy_command_buffers{};

        size_t TargetByteSize() const {
            return static_cast<size_t>(m_extent.width) * m_extent.height * 4u; // RGBA8
        }

        void AllocateTargets(const vk::Device &device) {
            m_targets.clear();
            m_targets.resize(m_image_count);
            for (uint32_t i = 0; i < m_image_count; i++) {
                m_targets[i] = Rhi::DeviceBuffer::CreateUnique(
                    *m_allocator,
                    Rhi::BufferType{Rhi::BufferTypeBits::ReadbackFromDevice},
                    TargetByteSize(),
                    "Offscreen present target " + std::to_string(i)
                );
            }

            m_copy_command_buffers.clear();
            auto cbai = vk::CommandBufferAllocateInfo{
                m_device_interface->GetQueueInfo().graphicsPool.get(), vk::CommandBufferLevel::ePrimary, m_image_count
            };
            m_copy_command_buffers = device.allocateCommandBuffersUnique(cbai);
        }

        bool IsAllocated() const {
            return m_targets.size() == m_image_count && m_targets[0] != nullptr;
        }
    };

    OffscreenPresentProvider::OffscreenPresentProvider(
        const Rhi::DeviceInterface &device_interface,
        const Rhi::AllocatorState &allocator,
        vk::Extent2D extent,
        vk::Format color_format,
        uint32_t image_count
    ) : pimpl(std::make_unique<impl>(impl{&device_interface, &allocator, extent, color_format, image_count})) {
    }

    OffscreenPresentProvider::~OffscreenPresentProvider() = default;

    vk::Extent2D OffscreenPresentProvider::GetExtent() const {
        return pimpl->m_extent;
    }

    vk::Format OffscreenPresentProvider::GetColorFormat() const {
        return pimpl->m_color_format;
    }

    uint32_t OffscreenPresentProvider::GetImageCount() const {
        return pimpl->m_image_count;
    }

    uint32_t OffscreenPresentProvider::AcquireNextImage(vk::Device, vk::Semaphore image_ready_semaphore, uint64_t) {
        auto image_index = (pimpl->m_frame_counter++) % pimpl->m_image_count;

        // Fulfill the acquire contract: the image is "available" immediately,
        // so signal image_ready via an empty submit (no command buffer).
        vk::SemaphoreSubmitInfo signal_info{image_ready_semaphore, 0, vk::PipelineStageFlagBits2::eAllCommands};
        vk::SubmitInfo2 sinfo{};
        sinfo.signalSemaphoreInfoCount = 1;
        sinfo.pSignalSemaphoreInfos = &signal_info;
        pimpl->m_device_interface->GetQueueInfo().graphicsQueue.submit2(sinfo, nullptr);
        return image_index;
    }

    vk::CommandBuffer OffscreenPresentProvider::PrepareCopy(
        vk::Device device,
        const RenderTargetTexture &final_rtt,
        uint32_t image_index,
        Rhi::MemoryAccessTypeImageBits last_access
    ) {
        // Lazy allocation: pure-physics headless loops that never render
        // allocate nothing.
        if (!pimpl->IsAllocated()) {
            pimpl->AllocateTargets(device);
        }

        auto cb = pimpl->m_copy_command_buffers[image_index].get();
        cb.reset();

        vk::CommandBufferBeginInfo begin_info{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
        cb.begin(begin_info);
        RecordCopyImageToBuffer(cb, final_rtt, *pimpl->m_targets[image_index], last_access);
        cb.end();
        return cb;
    }

    bool OffscreenPresentProvider::Present(vk::Device, uint32_t, vk::Semaphore) {
        // No Vulkan presentation: the frame-completion batch already carries a
        // real copy; waiting here is delegated to FrameManager's batch.
        return false;
    }

    void OffscreenPresentProvider::Recreate(vk::Extent2D new_extent) {
        pimpl->m_extent = new_extent;
    }
} // namespace Engine::RenderSystemState
