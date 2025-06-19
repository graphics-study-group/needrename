#ifndef RENDER_MEMORY_TEXTURE_INCLUDED
#define RENDER_MEMORY_TEXTURE_INCLUDED

#include <vulkan/vulkan.hpp>
#include "Render/ImageUtils.h"

namespace Engine {
    class RenderSystem;
    class AllocatedMemory;
    class Buffer;

    class Texture {
    public:
        struct TextureDesc {
            uint32_t dimensions;
            uint32_t width, height, depth;
            ImageUtils::ImageFormat format;
            ImageUtils::ImageType type;
            uint32_t mipmap_levels;
            uint32_t array_layers;
            bool is_cube_map;
        };

    protected:
        RenderSystem & m_system;
        TextureDesc m_desc;
        std::unique_ptr <AllocatedMemory> m_image;
        vk::UniqueImageView m_image_view;
        std::string m_name;

    public:
        Texture(RenderSystem &) noexcept;
        ~Texture();
        
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

        const TextureDesc & GetTextureDescription() const noexcept;

        vk::Image GetImage() const noexcept;

        vk::ImageView GetImageView() const noexcept;

        Buffer CreateStagingBuffer() const;
    };
}

#endif // RENDER_MEMORY_TEXTURE_INCLUDED
