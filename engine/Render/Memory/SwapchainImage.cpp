#include "SwapchainImage.h"

namespace Engine {
    SwapchainImage::SwapchainImage(
        std::vector<vk::Image> _images, std::vector<vk::ImageView> _image_views
    ) : m_images(_images), m_image_views(_image_views)
    {
    }
    vk::Image SwapchainImage::GetImage(uint32_t frame) const
    {
        assert(frame < m_images.size());
        return m_images[frame];
    }
    vk::ImageView SwapchainImage::GetImageView(uint32_t frame) const
    {
        assert(frame < m_image_views.size());
        return m_image_views[frame];
    }
}
