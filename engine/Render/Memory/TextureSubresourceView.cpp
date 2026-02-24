#include "TextureSubresourceView.h"

#include "Render/Memory/Texture.h"

namespace Engine{
    vk::ImageView TextureSubresourceView::GetImageView() {
        return texture.GetImageView(this->range);
    }
    vk::ImageView TextureSubresourceRange::GetImageView(Texture &t) const {
        return t.GetImageView(*this);
    }
} // namespace Engine
