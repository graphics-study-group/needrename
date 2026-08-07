#include "TextureSubresourceView.h"

#include "Rhi/Texture.h"

namespace Engine::Rhi {
    vk::ImageView TextureSubresourceView::GetImageView() {
        return texture.GetImageView(this->range);
    }
    vk::ImageView TextureSubresourceRange::GetImageView(Texture &t) const {
        return t.GetImageView(*this);
    }
} // namespace Engine::Rhi