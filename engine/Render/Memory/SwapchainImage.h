#ifndef RENDER_MEMORY_SWAPCHAINIMAGE_INCLUDED
#define RENDER_MEMORY_SWAPCHAINIMAGE_INCLUDED

#include "Render/Memory/ImageInterface.h"

namespace Engine {
    class SwapchainImage : public ImageInterface {
        vk::Image m_image;
        vk::ImageView m_image_view;
    public:
        SwapchainImage(vk::Image _image, vk::ImageView _image_view);

        vk::Image GetImage() const override;
        vk::ImageView GetImageView() const override;
    };
}

#endif // RENDER_MEMORY_SWAPCHAINIMAGE_INCLUDED
