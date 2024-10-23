#include "RenderImageTexture.h"

namespace Engine {
    RenderImageTexture::RenderImageTexture(
        std::vector<vk::Image> _images, 
        std::vector<vk::ImageView> _image_views
    ) : m_images(_images), m_image_views(_image_views)
    {
    }

    vk::Image RenderImageTexture::GetImage(uint32_t frame) const
    {
        assert(frame < m_images.size());
        return m_images[frame];
    }
    vk::ImageView RenderImageTexture::GetImageView(uint32_t frame) const
    {
        assert(frame < m_image_views.size());
        return m_image_views[frame];
    }
    void RenderImageTexture::SetLoadOperation(LoadOperation op)
    {
        m_load_op = op;
    }
    void RenderImageTexture::SetStoreOperation(StoreOperation op)
    {
        m_store_op = op;
    }
    RenderImageTexture::LoadOperation RenderImageTexture::GetLoadOperation() const
    {
        return m_load_op;
    }
    RenderImageTexture::StoreOperation RenderImageTexture::GetStoreOperation() const
    {
        return m_store_op;
    }
}
