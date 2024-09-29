#ifndef RENDER_MEMORY_IMAGEINTERFACE_INCLUDED
#define RENDER_MEMORY_IMAGEINTERFACE_INCLUDED

#include <vulkan/vulkan.hpp>

namespace Engine {
    class ImageInterface {
    public:
        virtual ~ImageInterface() = default;
        virtual vk::Image GetImage() const = 0;
        virtual vk::ImageView GetImageView() const = 0;
    };

    class ImagePerFrameInterface {
    public:
        virtual ~ImagePerFrameInterface() = default;
        virtual vk::Image GetImage(uint32_t frame) const = 0;
        virtual vk::ImageView GetImageView(uint32_t frame) const = 0;
    };
}

#endif // RENDER_MEMORY_IMAGEINTERFACE_INCLUDED
