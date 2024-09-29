#ifndef RENDER_MEMORY_IMAGE_INCLUDED
#define RENDER_MEMORY_IMAGE_INCLUDED

#include <vulkan/vulkan.hpp>
#include "Render/Memory/ImageInterface.h"

namespace Engine {

    class RenderSystem;

    class AllocatedImage2D : public ImageInterface {
    public:
        enum class ImageType {
            DepthImage,
            DepthImageTransient,
            DepthStencilImage,
            TextureImage
        };

    protected:
        std::weak_ptr <RenderSystem> m_system;

        vk::UniqueImage m_image {};
        /// TODO: Rewrite with a Vulkan allocator library
        vk::UniqueDeviceMemory m_memory {};

        vk::UniqueImageView m_view {};

        vk::Extent2D m_extent{};
        uint32_t m_mip_level {};
        vk::Format m_format {};
        
        static uint16_t GetFormatSize(vk::Format format);
        static vk::MemoryPropertyFlags GetMemoryProperty(ImageType type);
        static vk::ImageUsageFlags GetImageUsage(ImageType type);
        static vk::ImageAspectFlags GetImageAspect(ImageType type);
    
    public:

        AllocatedImage2D(std::weak_ptr <RenderSystem> system);

        void Create(uint32_t width, uint32_t height, ImageType type, vk::Format format, uint32_t mip = 1);

        vk::Image GetImage() const override;
        vk::ImageView GetImageView() const override;

        vk::DeviceMemory GetMemory() const;

        vk::Extent2D GetExtent() const;
    };
};

#endif // RENDER_MEMORY_IMAGE_INCLUDED
