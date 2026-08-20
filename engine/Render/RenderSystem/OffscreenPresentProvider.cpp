#include "OffscreenPresentProvider.h"

#include "Render/Resource/MemoryAccessHelper.hpp"
#include "Render/Resource/RenderTargetTexture.h"
#include "Rhi/Buffer/DeviceBuffer.h"
#include "Rhi/Device/AllocatorState.h"
#include "Rhi/Device/DeviceInterface.h"
#include "Rhi/Device/MemoryAccessTypes.h"
#include "Rhi/Device/MemoryTypes.h"

namespace Engine::RenderSystemState {

    namespace {
        /**
         * @brief Record a CPU-readable copy of a final RTT into a present target.
         *
         * The present provider has no Engine::CommandBuffer wrapper, so it
         * records the copy directly: a pre-barrier into `eTransferSrcOptimal`, a
         * tight-packed `copyImageToBuffer` of mip 0 at the texture's full extent,
         * then a post-barrier restoring `GetImageLayout(last_access)`.
         *
         * @param cb          Command buffer to record into.
         * @param image       Source image to copy from.
         * @param dst         Destination buffer (host-visible present target).
         * @param last_access Access mode the source image was last used with.
         */
        void RecordCopyImageToBuffer(
            vk::CommandBuffer cb,
            const RenderTargetTexture &image,
            const Rhi::DeviceBuffer &dst,
            Rhi::MemoryAccessTypeImageBits last_access
        ) {
            const auto &desc = image.GetTextureDescription();

            vk::ImageMemoryBarrier2 pre_barrier{
                vk::PipelineStageFlagBits2::eAllCommands,
                GetAccessFlags({last_access}),
                vk::PipelineStageFlagBits2::eAllTransfer,
                vk::AccessFlagBits2::eTransferRead,
                GetImageLayout({last_access}),
                vk::ImageLayout::eTransferSrcOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                image.GetImage(),
                vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
            };
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, pre_barrier});

            vk::BufferImageCopy region{
                0, // bufferOffset
                0, // bufferRowLength (tight packing)
                0, // bufferImageHeight
                vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                vk::Offset3D{0, 0, 0},
                vk::Extent3D{desc.width, desc.height, desc.depth}
            };
            cb.copyImageToBuffer(image.GetImage(), vk::ImageLayout::eTransferSrcOptimal, dst.GetBuffer(), region);

            vk::ImageMemoryBarrier2 post_barrier{
                vk::PipelineStageFlagBits2::eAllTransfer,
                vk::AccessFlagBits2::eTransferRead,
                vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eMemoryRead,
                vk::ImageLayout::eTransferSrcOptimal,
                GetImageLayout({last_access}),
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                image.GetImage(),
                vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
            };
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, post_barrier});
        }
    } // namespace
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
