#include "Image2DTexture.h"

#include "Render/Memory/Buffer.h"
#include "Asset/Texture/Image2DTextureAsset.h"

namespace Engine {
    AllocatedImage2DTexture::AllocatedImage2DTexture(std::weak_ptr<RenderSystem> system) : AllocatedImage2D(system) {
    }

    void AllocatedImage2DTexture::Create(const Image2DTextureAsset &asset) {
        AllocatedImage2D::Create(
            asset.m_width, 
            asset.m_height, 
            ImageUtils::ImageType::TextureImage, 
            asset.m_format, 
            asset.m_mip_level,
            asset.m_name
        );
    }

    void AllocatedImage2DTexture::Create(uint32_t width, uint32_t height, ImageUtils::ImageFormat format, uint32_t mip, const std::string & name) {
        AllocatedImage2D::Create(width, height, ImageUtils::ImageType::TextureImage, format, mip, name);
    }

    Buffer AllocatedImage2DTexture::CreateStagingBuffer() const {
        uint64_t buffer_size = m_extent.height * m_extent.width * ImageUtils::GetPixelSize(m_format);
        assert(buffer_size > 0);

        Buffer buffer{m_system};
        buffer.Create(Buffer::BufferType::Staging, buffer_size, "Buffer - texture staging");
        return buffer;
    }
}
