#ifndef RENDER_MEMORY_IMAGE_INCLUDED
#define RENDER_MEMORY_IMAGE_INCLUDED

#include <vulkan/vulkan.hpp>

namespace Engine {

    class RenderSystem;

    class AllocatedImage2D {
    public:
        enum class ImageType {
            DepthImage,
            DepthStencilImage,
            TextureImage
        };

    protected:
        std::weak_ptr <RenderSystem> m_system;

        vk::UniqueImage m_image {};
        /// TODO: Rewrite with a Vulkan allocator library
        vk::UniqueDeviceMemory m_memory {};

        vk::UniqueImageView m_view {};

        static vk::MemoryPropertyFlags GetMemoryProperty(ImageType type);
        static vk::ImageUsageFlags GetImageUsage(ImageType type);
        static vk::ImageAspectFlags GetImageAspect(ImageType type);
    
    public:

        AllocatedImage2D(std::weak_ptr <RenderSystem> system);

        void Create(uint32_t width, uint32_t height, ImageType type, vk::Format format, uint32_t mip = 1);

        vk::Image GetImage() const;
        vk::DeviceMemory GetMemory() const;
        vk::ImageView GetImageView() const;
    };
};

#endif // RENDER_MEMORY_IMAGE_INCLUDED
