#include "HeadlessPresentProvider.h"

#include "Rhi/DeviceInterface.h"

namespace Engine::RenderSystemState {
    struct HeadlessPresentProvider::impl {
        const DeviceInterface *m_device_interface;
        vk::Extent2D m_extent;
        vk::Format m_color_format;
        uint32_t m_image_count;
        uint32_t m_frame_counter = 0;
    };

    HeadlessPresentProvider::HeadlessPresentProvider(
        const DeviceInterface &device_interface, vk::Extent2D extent, vk::Format color_format, uint32_t image_count
    ) : pimpl(std::make_unique<impl>(impl{&device_interface, extent, color_format, image_count})) {
    }

    HeadlessPresentProvider::~HeadlessPresentProvider() = default;

    vk::Extent2D HeadlessPresentProvider::GetExtent() const {
        return pimpl->m_extent;
    }

    vk::Format HeadlessPresentProvider::GetColorFormat() const {
        return pimpl->m_color_format;
    }

    uint32_t HeadlessPresentProvider::GetImageCount() const {
        return pimpl->m_image_count;
    }

    uint32_t HeadlessPresentProvider::AcquireNextImage(vk::Device, vk::Semaphore image_ready_semaphore, uint64_t) {
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

    vk::CommandBuffer HeadlessPresentProvider::PrepareCopy(
        vk::Device, const RenderTargetTexture &, uint32_t, MemoryAccessTypeImageBits
    ) {
        // No presentation target — the frame-completion batch carries no copy CB.
        return nullptr;
    }

    bool HeadlessPresentProvider::Present(vk::Device, uint32_t, vk::Semaphore) {
        return false;
    }

    void HeadlessPresentProvider::Recreate(vk::Extent2D new_extent) {
        pimpl->m_extent = new_extent;
    }
} // namespace Engine::RenderSystemState
