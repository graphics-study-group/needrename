#include "SwapchainImage.h"

namespace Engine {
    SwapchainImage::SwapchainImage(vk::Image _image, vk::ImageView _image_view) 
        : m_image(_image), m_image_view(_image_view)
    {
    }
    vk::Image SwapchainImage::GetImage() const
    {
        return m_image;
    }
    vk::ImageView SwapchainImage::GetImageView() const
    {
        return m_image_view;
    }
}
