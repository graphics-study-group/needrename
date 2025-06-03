#include "Texture.h"

#include "Render/RenderSystem.h"
#include "Render/Memory/Buffer.h"
#include "Render/Memory/AllocatedMemory.h"
#include "Render/RenderSystem/AllocatorState.h"

namespace Engine {
    Texture::Texture(RenderSystem & system) : m_system(system)
    {
    }

    Texture::~Texture() = default;

    void Texture::CreateTexture(
        uint32_t dimension, 
        uint32_t width, 
        uint32_t height, 
        uint32_t depth, 
        ImageUtils::ImageFormat format, 
        ImageUtils::ImageType type, 
        uint32_t mipLevels, 
        uint32_t arrayLayers,
        bool isTextureArray,
        bool isCubeMap,
        const std::string &name
    ) {
        auto & allocator = m_system.GetAllocatorState();

        // Some prelimary checks
        assert(1 <= dimension && dimension <= 3);
        assert(width >= 1 && height >= 1 && depth >= 1);
        assert(mipLevels >= 1);
        assert(arrayLayers >= 1);

        auto dim = dimension == 1 ? vk::ImageType::e1D : (dimension == 2 ? vk::ImageType::e2D : vk::ImageType::e3D);
        this->m_image = allocator.AllocateImageUniqueEx(
            type,
            dim,
            vk::Extent3D{width, height, depth},
            ImageUtils::GetVkFormat(format),
            mipLevels,
            arrayLayers,
            vk::SampleCountFlagBits::e1,
            name
        );

        vk::ImageViewType view_type;
        if (isCubeMap) {
            assert(arrayLayers > 0 && arrayLayers % 6 == 0);
            view_type = arrayLayers > 6 ? vk::ImageViewType::eCubeArray : vk::ImageViewType::eCube;
        } else {
            switch(dim) {
                case vk::ImageType::e1D:
                    view_type = isTextureArray ? vk::ImageViewType::e1DArray : vk::ImageViewType::e1D;
                    break;
                case vk::ImageType::e2D:
                    view_type = isTextureArray ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D;
                    break;
                case vk::ImageType::e3D:
                    assert(!isTextureArray);
                    view_type = vk::ImageViewType::e3D;
                    break;
            }
        }

        vk::ImageViewCreateInfo vinfo {
            vk::ImageViewCreateFlags{},
            m_image->GetImage(),
            view_type,
            ImageUtils::GetVkFormat(format),
            vk::ComponentMapping {},
            vk::ImageSubresourceRange {
                ImageUtils::GetVkImageAspect(type),
                0, mipLevels, 0, arrayLayers
            }
        };
        m_image_view = m_system.getDevice().createImageViewUnique(vinfo);

        this->m_desc = TextureDesc{
            .dimension = dim,
            .width = width, .height = height, .depth = depth,
            .format = ImageUtils::GetVkFormat(format),
            .view_type = view_type,
            .mipmap_levels = mipLevels,
            .array_layers = arrayLayers
        };
    }

    const Texture::TextureDesc &Texture::GetTextureDescription() const noexcept
    {
        return this->m_desc;
    }

    vk::Image Engine::Texture::GetImage() const noexcept
    {
        return this->m_image->GetImage();
    }

    vk::ImageView Engine::Texture::GetImageView() const noexcept
    {
        return this->m_image_view.get();
    }

    Buffer Engine::Texture::CreateStagingBuffer() const
    {
        uint64_t buffer_size = m_desc.height * m_desc.width * m_desc.depth * ImageUtils::GetPixelSize(m_desc.m_format);
        assert(buffer_size > 0);

        Buffer buffer{m_system};
        buffer.Create(Buffer::BufferType::Staging, buffer_size, "Buffer - texture staging");
        return buffer;
    }
}

