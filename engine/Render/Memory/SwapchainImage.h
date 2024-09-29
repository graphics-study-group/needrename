#ifndef RENDER_MEMORY_SWAPCHAINIMAGE_INCLUDED
#define RENDER_MEMORY_SWAPCHAINIMAGE_INCLUDED

#include "Render/Memory/ImageInterface.h"
#include <vector>

namespace Engine {
    class SwapchainImage : public ImagePerFrameInterface {
        std::vector <vk::Image> m_images;
        std::vector <vk::ImageView> m_image_views;
    public:
        SwapchainImage(std::vector <vk::Image> _images, std::vector <vk::ImageView> _image_views);

        vk::Image GetImage(uint32_t frame) const override;
        vk::ImageView GetImageView(uint32_t frame) const override;
    };
}

#endif // RENDER_MEMORY_SWAPCHAINIMAGE_INCLUDED
