#include "Image2DTexture.h"

#include "Render/Memory/Buffer.h"

namespace Engine {
    AllocatedImage2DTexture::AllocatedImage2DTexture(std::weak_ptr<RenderSystem> system) : AllocatedImage2D(system) {
    }

    void AllocatedImage2DTexture::Create(uint32_t width, uint32_t height, vk::Format format, uint32_t mip) {
        AllocatedImage2D::Create(width, height, AllocatedImage2D::ImageType::TextureImage, format, mip);
    }

    Buffer AllocatedImage2DTexture::CreateStagingBuffer() const {
        uint64_t buffer_size = m_extent.height * m_extent.width * GetFormatSize(m_format);
        assert(buffer_size > 0);

        Buffer buffer{m_system};
        buffer.Create(Buffer::BufferType::Staging, buffer_size);
        return buffer;
    }
}
