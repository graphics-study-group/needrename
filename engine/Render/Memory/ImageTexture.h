#ifndef RENDER_MEMORY_IMAGETEXTURE_INCLUDED
#define RENDER_MEMORY_IMAGETEXTURE_INCLUDED

#include "Texture.h"

namespace Engine {
    class Image2DTextureAsset;
    class ImageCubemapAsset;
    /**
     * @brief A read-only image texture.
     * Its content must be transferred from the CPU side, and
     * cannot be modified by any render operation.
     */
    class ImageTexture : public Texture {
    public:
        
        /**
         * @brief Description of an image texture.
         * 
         * @see `Engine::ImageUtils::TextureDesc`
         */
        struct ImageTextureDesc {
#define COPY_ENUM_VALUE(x) x = (int)ImageUtils::ImageFormat::x
            enum class ImageTextureFormat {
                COPY_ENUM_VALUE(R8G8B8A8SNorm),
                COPY_ENUM_VALUE(R8G8B8A8UNorm),
                COPY_ENUM_VALUE(R8G8B8A8SRGB),
            };
#undef COPY_ENUM_VALUE

            uint32_t dimensions;
            uint32_t width, height, depth;
            uint32_t mipmap_levels;
            uint32_t array_layers;
            ImageTextureFormat format;
            bool is_cube_map;
        };
        using ITFormat = ImageTextureDesc::ImageTextureFormat;

    protected:
        ImageTexture(RenderSystem &system, TextureDesc texture, SamplerDesc sampler, const std::string &name = "");

    public:

        /**
         * @brief Create a texture from descriptions.
         */
        static std::unique_ptr<ImageTexture> CreateUnique(
            RenderSystem &system, ImageTextureDesc texture, SamplerDesc sampler, const std::string &name = ""
        );

        /**
         * @brief Create a texture from an asset.
         * 
         * Width and height will be read from the asset. Its format will be
         * defaulted to R8G8B8A8 SRGB. Its attached sampler will be defaulted.
         */
        static std::unique_ptr<ImageTexture> CreateUnique(RenderSystem &system, const Image2DTextureAsset &asset);

        /**
         * @brief Create a cubemap from an asset.
         * 
         * Width and height will be read from the asset. Its format will be
         * defaulted to R8G8B8A8 SRGB.
         */
        static std::unique_ptr<ImageTexture> CreateUnique(RenderSystem &system, const ImageCubemapAsset &asset);
    };
} // namespace Engine

#endif // RENDER_MEMORY_IMAGETEXTURE_INCLUDED
