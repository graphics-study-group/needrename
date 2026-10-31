#include "Rhi/Texture/TextureSubresourceView.h"
#include "Rhi/Backend/Vulkan/VulkanHandles.h"

#include "Rhi/Texture/Texture.h"

namespace Engine::Rhi {
    ImageViewHandle TextureSubresourceView::GetImageView() {
        return texture.GetImageView(this->range);
    }
    ImageViewHandle TextureSubresourceRange::GetImageView(Texture &t) const {
        return t.GetImageView(*this);
    }
} // namespace Engine::Rhi




