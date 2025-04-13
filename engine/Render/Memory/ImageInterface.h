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
}

#endif // RENDER_MEMORY_IMAGEINTERFACE_INCLUDED
