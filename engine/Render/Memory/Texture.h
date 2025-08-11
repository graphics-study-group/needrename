#ifndef RENDER_MEMORY_TEXTURE_INCLUDED
#define RENDER_MEMORY_TEXTURE_INCLUDED

#include "Render/ImageUtils.h"
#include "Render/Memory/TextureSlice.h"
#include <memory>

namespace vk {
    class Image;
    class ImageView;
} // namespace vk

namespace Engine {
    class RenderSystem;
    class AllocatedMemory;
    class Buffer;

    class Texture {
    public:
        using TextureDesc = ImageUtils::TextureDesc;

    protected:
        RenderSystem &m_system;
        TextureDesc m_desc;
        std::unique_ptr<AllocatedMemory> m_image;
        std::unique_ptr<SlicedTextureView> m_full_view;
        std::string m_name;

    public:
        Texture(RenderSystem &) noexcept;
        virtual ~Texture();

        void CreateTexture(
            uint32_t dimensions,
            uint32_t width,
            uint32_t height,
            uint32_t depth,
            ImageUtils::ImageFormat format,
            ImageUtils::ImageType type,
            uint32_t mipLevels,
            uint32_t arrayLayers = 1,
            bool isCubeMap = false,
            std::string name = ""
        );

        void CreateTexture(TextureDesc desc, std::string name = "");

        const TextureDesc &GetTextureDescription() const noexcept;

        vk::Image GetImage() const noexcept;

        const SlicedTextureView &GetFullSlice() const noexcept;

        vk::ImageView GetImageView() const noexcept;

        Buffer CreateStagingBuffer() const;
    };
} // namespace Engine

#endif // RENDER_MEMORY_TEXTURE_INCLUDED
