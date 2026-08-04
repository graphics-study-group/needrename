#include "HeadlessPresentProvider.h"

namespace Engine::RenderSystemState {
    struct HeadlessPresentProvider::impl {
        vk::Extent2D m_extent;
        vk::Format m_color_format;
        uint32_t m_image_count;
        uint32_t m_frame_counter = 0;
    };

    HeadlessPresentProvider::HeadlessPresentProvider(
        vk::Extent2D extent, vk::Format color_format, uint32_t image_count
    ) : pimpl(std::make_unique<impl>(impl{extent, color_format, image_count})) {
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

    uint32_t HeadlessPresentProvider::AcquireNextImage(vk::Device, vk::Semaphore, uint64_t) {
        return (pimpl->m_frame_counter++) % pimpl->m_image_count;
    }

    bool HeadlessPresentProvider::CompleteFrame(
        vk::Device, const RenderTargetTexture &, uint32_t, const FrameSyncInfo &
    ) {
        return false;
    }

    void HeadlessPresentProvider::Recreate(vk::Extent2D new_extent) {
        pimpl->m_extent = new_extent;
    }
} // namespace Engine::RenderSystemState
