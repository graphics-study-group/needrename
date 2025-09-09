#ifndef ENGINE_RENDER_IMAGEUTILS_INCLUDED
#define ENGINE_RENDER_IMAGEUTILS_INCLUDED

namespace Engine {
    namespace ImageUtils {
        enum class ImageType {
            DepthAttachment,
            DepthStencilAttachment,
            ColorAttachment,
            // Color image used for sampling. Can be transferred from/to.
            TextureImage
        };

        enum class ImageFormat {
            UNDEFINED,
            R8G8B8A8SNorm,
            R8G8B8A8UNorm,
            // 32-bit packed float for HDR rendering. In Vulkan, it is actually B10-G11-R11.
            R11G11B10UFloat,
            R32G32B32A32SFloat,
            D32SFLOAT,
        };

        struct TextureDesc {
            uint32_t dimensions;
            uint32_t width, height, depth;
            ImageUtils::ImageFormat format;
            ImageUtils::ImageType type;
            uint32_t mipmap_levels;
            uint32_t array_layers;
            bool is_cube_map;
        };
    } // namespace ImageUtils
} // namespace Engine

#endif // ENGINE_RENDER_IMAGEUTILS_INCLUDED
