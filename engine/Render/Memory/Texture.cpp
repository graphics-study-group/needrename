#include "Texture.h"

#include "Render/RenderSystem.h"
#include "Render/Memory/Buffer.h"
#include "Render/Memory/AllocatedMemory.h"
#include "Render/RenderSystem/AllocatorState.h"

namespace Engine {

    Texture::Texture(RenderSystem & system) noexcept : m_system(system), m_full_view(nullptr)
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
        bool isCubeMap,
        std::string name
    ) {
        
        this->CreateTexture(TextureDesc{
                .dimensions = dimension,
                .width = width,
                .height = height,
                .depth = depth,
                .format = format,
                .type = type,
                .mipmap_levels = mipLevels,
                .array_layers = arrayLayers,
                .is_cube_map = isCubeMap
            }, 
            name
        );
    }

    void Texture::CreateTexture(TextureDesc desc, std::string name)
    {
        auto & allocator = m_system.GetAllocatorState();
        auto dimension = desc.dimensions;
        auto [width, height, depth] = std::tie(desc.width, desc.height, desc.depth);
        auto mipLevels = desc.mipmap_levels;
        auto arrayLayers = desc.array_layers;

        // Some prelimary checks
        assert(1 <= dimension && dimension <= 3);
        assert(width >= 1 && height >= 1 && depth >= 1);
        assert(dimension != 1 || (height == 1 && depth == 1));
        assert(dimension != 2 || (depth == 1));
        assert(mipLevels >= 1);
        assert(arrayLayers >= 1);

        auto dim = dimension == 1 ? vk::ImageType::e1D : (dimension == 2 ? vk::ImageType::e2D : vk::ImageType::e3D);
        this->m_image = allocator.AllocateImageUniqueEx(
            desc.type,
            dim,
            vk::Extent3D{width, height, depth},
            ImageUtils::GetVkFormat(desc.format),
            mipLevels,
            arrayLayers,
            vk::SampleCountFlagBits::e1,
            name
        );
        this->m_desc = desc;
        this->m_name = name;

        m_full_view = std::make_unique<SlicedTextureView>(m_system, *this, TextureSlice{0, desc.mipmap_levels, 0, desc.array_layers});
    }

    const Texture::TextureDesc &Texture::GetTextureDescription() const noexcept
    {
        return this->m_desc;
    }

    vk::Image Engine::Texture::GetImage() const noexcept
    {
        assert(this->m_image && this->m_image->GetImage());
        return this->m_image->GetImage();
    }

    const SlicedTextureView &Texture::GetFullSlice() const noexcept
    {
        assert(m_full_view);
        return *m_full_view;
    }

    vk::ImageView Engine::Texture::GetImageView() const noexcept
    {
        return this->GetFullSlice().GetImageView();
    }

    Buffer Engine::Texture::CreateStagingBuffer() const
    {
        assert(
            ((std::get<0>(ImageUtils::GetImageFlags(this->GetTextureDescription().type)) & vk::ImageUsageFlagBits::eTransferDst)
            || (std::get<0>(ImageUtils::GetImageFlags(this->GetTextureDescription().type)) & vk::ImageUsageFlagBits::eTransferSrc))
            && "A staging buffer is created, but the image does not support tranfer usage."
        );
        uint64_t buffer_size = m_desc.height * m_desc.width * m_desc.depth * ImageUtils::GetPixelSize(m_desc.format);
        assert(buffer_size > 0);

        Buffer buffer{m_system};
        buffer.Create(Buffer::BufferType::Staging, buffer_size, "Buffer - texture staging");
        return buffer;
    }
}

