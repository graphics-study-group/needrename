#include "Image2D.h"

#include "Render/RenderSystem.h"
#include "Render/RenderSystem/AllocatorState.h"

#include "Render/DebugUtils.h"

namespace Engine {

    AllocatedImage2D::AllocatedImage2D(std::weak_ptr<RenderSystem> system) : m_system(system) {
    }

    void AllocatedImage2D::Create(
        uint32_t width, 
        uint32_t height, 
        ImageUtils::ImageType type, 
        ImageUtils::ImageFormat format, 
        uint32_t mip,
        const std::string & name
    ) {
        auto system = m_system.lock();

        m_allocated_memory = system->GetAllocatorState().AllocateImageUnique(
            type, 
            VkExtent3D{width, height, 1}, 
            static_cast<VkFormat>(ImageUtils::GetVkFormat(format)),
            name
        );
        assert(m_allocated_memory);

        vk::ImageViewCreateInfo vinfo {
            vk::ImageViewCreateFlags{},
            m_allocated_memory->GetImage(),
            vk::ImageViewType::e2D,
            ImageUtils::GetVkFormat(format),
            vk::ComponentMapping {},
            vk::ImageSubresourceRange {
                ImageUtils::GetVkImageAspect(type),
                0, mip, 0, 1
            }
        };
        m_view = system->getDevice().createImageViewUnique(vinfo);

        m_format = format;
        m_extent = vk::Extent2D{width, height};
        m_mip_level = mip;
    }

    vk::Image AllocatedImage2D::GetImage() const {
        assert(m_allocated_memory);
        return m_allocated_memory->GetImage();
    }

    vk::ImageView AllocatedImage2D::GetImageView() const {
        assert(m_view);
        return m_view.get();
    }
    vk::Extent2D AllocatedImage2D::GetExtent() const {
        return m_extent;
    }
};
