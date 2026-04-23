#ifndef ENGINE_RENDER_IMAGEUTILS_INCLUDED
#define ENGINE_RENDER_IMAGEUTILS_INCLUDED

#include "Render/Memory/MemoryTypes.h"
#include "Render/Pipeline/PipelineEnums.h"
#include <Reflection/macros.h>

#include <cstdint>

namespace Engine {
    /**
     * @brief Utility for image operations
     */
    namespace ImageUtils {
        /**
         * @brief Available formats for images.
         *
         * Not all image formats supports all operations. For example, SRGB
         * images in general does not support storage (UAV) operation. Use
         * `Engine::RenderSystemState::AllocatorState::QueryFormatFeatures()`
         * to determine whether your use case is supported.
         */
        enum class REFL_SER_CLASS() ImageFormat {
            /// Undefined image format.
            UNDEFINED,
            /// R8-G8-B8-A8 image as signed normalized real numbers.
            R8G8B8A8SNorm,
            /// R8-G8-B8-A8 image as unsigned normalized real numbers.
            R8G8B8A8UNorm,
            /// R8-G8-B8-A8 image in sRGB space.
            /// Unordered access on this type of image is rarely supported.
            R8G8B8A8SRGB,
            /// 32-bit packed float for HDR rendering.
            /// In Vulkan, it is actually B10-G11-R11.
            R11G11B10UFloat,
            /// R32-G32-B32-A32 image as single precision floating points.
            R32G32B32A32SFloat,
            /// 32 bit floating point depth buffer.
            D32SFLOAT,
        };

        /// @brief Description of a texture
        struct TextureDesc {
            /// An integer between 1 and 3 for dimension.
            uint32_t dimensions;
            /// Integers at least 1 for width in pixel.
            uint32_t width;
            /// Integers at least 1 for height in pixel.
            uint32_t height;
            /// Integers at least 1 for depth in pixel.
            uint32_t depth;
            /// Format of this image
            ImageFormat format;
            /// Allowed memory access on this image.
            ImageMemoryType memory_type;
            /// @brief Mipmap levels of the texture.
            /// Use zero to automatically determine mipmap levels
            /// by `max(log(width), log(height), log(depth))`
            uint32_t mipmap_levels;
            /// @brief Array layers of the texture.
            uint32_t array_layers;
            /// @brief Whether the texture is a cubemap.
            bool is_cube_map;
        };

        /// @brief Description of a sampler.
        struct SamplerDesc {
            /**
             * @brief Address mode of the sampler.
             *
             * If clamp to border is enabled for one texture coordinate,
             * then all coordinate that clamp to border
             * must clamp to the same border color.
             */
            enum class REFL_SER_CLASS() AddressMode : uint8_t {
                Repeat,
                MirroredRepeat,
                ClampToEdge,
                ClampToBorder_TransparentBlack, ///< Clamp to [0.0, 0.0, 0.0, 0.0]
                ClampToBorder_OpaqueBlack,      ///< Clamp to [0.0, 0.0, 0.0, 1.0]
                ClampToBorder_OpaqueWhite       ///< Clamp to [1.0, 1.0, 1.0, 1.0]
            };

            /**
             * @brief Filter mode of the sampler.
             */
            enum class REFL_SER_CLASS() FilterMode : uint8_t {
                Point,
                Linear
            };

            /// @brief Comparator for depth sampling
            using DepthComparator = PipelineUtils::DSComparator;

            /// Filter mode if the texel is minified.
            FilterMode min_filter{FilterMode::Point};
            /// Filter mode if the texel is magnified.
            FilterMode max_filter{FilterMode::Point};
            /// Filter mode between mipmap levels.
            FilterMode mipmap_filter{FilterMode::Point};

            /// Address mode on u (width) axis
            AddressMode u_address{AddressMode::Repeat};
            /// Address mode on v (height) axis
            AddressMode v_address{AddressMode::Repeat};
            /// Address mode on w (depth) axis
            AddressMode w_address{AddressMode::Repeat};

            /// Bias and clamps on LoD (mipmaps).
            float bias_lod{0.0f}, min_lod{0.0f}, max_lod{0.0f};

            /// Anisotropy value clamp.
            /// Use any value less than 1.0 to disable anisotropic filtering.
            float max_anisotropy{0.0f};

            /**
             * @brief Comparator used for depth comparison.
             *
             * Depth comparison is enabled only if this enum is set to other
             * values than `Always`. It is pertient to the sampling only if
             * SPIR-V inst `OpImage*Dref*` (e.g. via GLSL `samplerXShadow` type)
             * is used in the shader.
             */
            DepthComparator comparator{DepthComparator::Always};

            /// Default elementwise equality operator
            bool operator==(const SamplerDesc &) const = default;
        };
    } // namespace ImageUtils
} // namespace Engine

#endif // ENGINE_RENDER_IMAGEUTILS_INCLUDED
