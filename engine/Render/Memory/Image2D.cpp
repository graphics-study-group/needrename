#include "Image2D.h"

#include "Render/RenderSystem.h"

namespace Engine {
    vk::MemoryPropertyFlags AllocatedImage2D::GetMemoryProperty(ImageType type) {
        switch(type) {
        case ImageType::DepthImageTransient:
            return vk::MemoryPropertyFlagBits::eDeviceLocal | vk::MemoryPropertyFlagBits::eLazilyAllocated;
        default:
            return vk::MemoryPropertyFlagBits::eDeviceLocal;
        }
    }

    uint16_t AllocatedImage2D::GetFormatSize(vk::Format format) {
        switch(format) {
        case vk::Format::eR8G8B8A8Srgb:
            return 4;
        case vk::Format::eR8G8B8Srgb:
            return 3;
        default:
            return 0;
        }
    }

    vk::ImageUsageFlags AllocatedImage2D::GetImageUsage(ImageType type) {
        switch (type) {
        case ImageType::DepthImageTransient:
            return vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransientAttachment;
        case ImageType::DepthStencilImage:
        case ImageType::DepthImage:
            return vk::ImageUsageFlagBits::eDepthStencilAttachment;
        case ImageType::TextureImage:
            return vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
        }
        __builtin_unreachable();
    }

    vk::ImageAspectFlags AllocatedImage2D::GetImageAspect(ImageType type) {
        switch (type) {
        case ImageType::DepthImageTransient:
        case ImageType::DepthImage:
            return vk::ImageAspectFlagBits::eDepth;
        case ImageType::DepthStencilImage:
            return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
        case ImageType::TextureImage:
            return vk::ImageAspectFlagBits::eColor;
        }
        __builtin_unreachable();
    }

    AllocatedImage2D::AllocatedImage2D(std::weak_ptr<RenderSystem> system) : m_system(system) {
    }

    void AllocatedImage2D::Create(uint32_t width, uint32_t height, ImageType type, vk::Format format, uint32_t mip) {
        vk::ImageCreateInfo info{
            vk::ImageCreateFlags{},
            vk::ImageType::e2D,
            format,
            {width, height, 1},
            mip,
            1,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            GetImageUsage(type),
            vk::SharingMode::eExclusive,
            {},
            vk::ImageLayout::eUndefined
        };
        auto system = m_system.lock();
        auto device = system->getDevice();
        m_image = device.createImageUnique(info);

        vk::MemoryRequirements req = device.getImageMemoryRequirements(m_image.get());
        vk::MemoryAllocateInfo minfo {
            req.size, system->FindPhysicalMemory(req.memoryTypeBits, GetMemoryProperty(type))
        };
        m_memory = device.allocateMemoryUnique(minfo);
        device.bindImageMemory(m_image.get(), m_memory.get(), 0);

        vk::ImageViewCreateInfo vinfo {
            vk::ImageViewCreateFlags{},
            m_image.get(),
            vk::ImageViewType::e2D,
            format,
            vk::ComponentMapping {},
            vk::ImageSubresourceRange {
                GetImageAspect(type),
                0, mip, 0, 1
            }
        };
        m_view = device.createImageViewUnique(vinfo);

        m_format = format;
        m_extent = vk::Extent2D{width, height};
        m_mip_level = mip;
    }

    vk::Image AllocatedImage2D::GetImage() const {
        return m_image.get();
    }

    vk::DeviceMemory AllocatedImage2D::GetMemory() const {
        return m_memory.get();
    }

    vk::ImageView AllocatedImage2D::GetImageView() const {
        return m_view.get();
    }
    vk::Extent2D AllocatedImage2D::GetExtent() const {
        return m_extent;
    }
};
