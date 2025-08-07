#ifndef RENDER_MEMORY_TEXTURE_INCLUDED
#define RENDER_MEMORY_TEXTURE_INCLUDED

#include <vulkan/vulkan.hpp>
#include "Render/ImageUtils.h"
#include "Render/Memory/TextureSlice.h"

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

        static constexpr vk::ImageViewType InferImageViewType(const TextureDesc & desc)
        {
            vk::ImageViewType view_type;
            if (desc.is_cube_map) {
                assert(desc.array_layers > 0 && desc.array_layers % 6 == 0);
                view_type = desc.array_layers > 6 ? vk::ImageViewType::eCubeArray : vk::ImageViewType::eCube;
            } else {
                switch(desc.dimensions) {
                    case 1:
                        view_type = desc.array_layers > 1 ? vk::ImageViewType::e1DArray : vk::ImageViewType::e1D;
                        break;
                    case 2:
                        view_type = desc.array_layers > 1 ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D;
                        break;
                    case 3:
                        assert(desc.array_layers == 1);
                        view_type = vk::ImageViewType::e3D;
                        break;
                    default:
                        assert(!"Cannot construct an texture image on spaces that cannot be embedded into 3D Riemmanian manifolds.");
                }
            }
            return view_type;
        }

    protected:
        RenderSystem & m_system;
        TextureDesc m_desc;
        std::unique_ptr <AllocatedMemory> m_image;
        std::unique_ptr <SlicedTextureView> m_full_view;
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

        const TextureDesc & GetTextureDescription() const noexcept;

        vk::Image GetImage() const noexcept;

        const SlicedTextureView & GetFullSlice() const noexcept;

        vk::ImageView GetImageView() const noexcept;

        Buffer CreateStagingBuffer() const;
    };
}

#endif // RENDER_MEMORY_TEXTURE_INCLUDED
