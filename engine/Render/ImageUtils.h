#ifndef ENGINE_RENDER_IMAGEUTILS_INCLUDED
#define ENGINE_RENDER_IMAGEUTILS_INCLUDED

namespace Engine {
    namespace ImageUtils {
        enum class ImageType {
            DepthImage,
            DepthStencilImage,
            SampledDepthImage,
            // Color image used for sampling. Can be transferred from/to.
            TextureImage,
            // Color attachment image used for rendering. Can be transferred from/to.
            ColorAttachment,
            // Color storage image used for compute shader. Can be transferred from/to.
            ColorCompute,
            // General color texture image, suitable for transfering from/to, rendering and sampling.
            // Seems vk::ImageUsageFlags has no performance impact for desktop GPUs.
            ColorGeneral
        };

        enum class ImageFormat {
            UNDEFINED,
            B8G8R8A8SRGB,
            R8G8B8A8SRGB,
            R8G8B8A8SNorm,
            R8G8B8A8UNorm,
            // 32-bit packed float for HDR rendering. In Vulkan, it is actually B10-G11-R11.
            R11G11B10UFloat,
            R8G8B8SRGB,
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
    }
}

#endif // ENGINE_RENDER_IMAGEUTILS_INCLUDED
