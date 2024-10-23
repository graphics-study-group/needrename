#include "SwapchainImage.h"

namespace Engine {
    SwapchainImage::SwapchainImage(
        std::vector<vk::Image> _images, 
        std::vector<vk::ImageView> _image_views,
        bool is_depth
    ) : RenderImageTexture(_images, _image_views)
    {
        if (is_depth) {
            SetLoadOperation(AttachmentUtils::LoadOperation::Clear);
            SetStoreOperation(AttachmentUtils::StoreOperation::DontCare);
        } else {
            SetLoadOperation(AttachmentUtils::LoadOperation::Clear);
            SetStoreOperation(AttachmentUtils::StoreOperation::Store);
        }
    }
}
