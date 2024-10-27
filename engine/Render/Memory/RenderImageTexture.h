#ifndef RENDER_MEMORY_RENDERIMAGETEXTURE_INCLUDED
#define RENDER_MEMORY_RENDERIMAGETEXTURE_INCLUDED

#include "Render/Memory/ImageInterface.h"
#include "Render/AttachmentUtils.h"

namespace Engine {
    class RenderImageTexture : public ImagePerFrameInterface {
        std::vector <vk::Image> m_images;
        std::vector <vk::ImageView> m_image_views;

        using LoadOperation = AttachmentUtils::LoadOperation;
        using StoreOperation = AttachmentUtils::StoreOperation;

        LoadOperation m_load_op{LoadOperation::DontCare};
        StoreOperation m_store_op{StoreOperation::DontCare};

    public:
        RenderImageTexture(
            std::vector <vk::Image> _images, 
            std::vector <vk::ImageView> _image_views
        );
        virtual ~RenderImageTexture() = default;

        vk::Image GetImage(uint32_t frame) const override;
        vk::ImageView GetImageView(uint32_t frame) const override;

        void SetLoadOperation(LoadOperation op);
        void SetStoreOperation(StoreOperation op);
        LoadOperation GetLoadOperation() const;
        StoreOperation GetStoreOperation() const;
    };
}

#endif // RENDER_MEMORY_RENDERIMAGETEXTURE_INCLUDED
