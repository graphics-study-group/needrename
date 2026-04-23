#ifndef RENDER_PIPELINE_PIPELINEENUMS_INCLUDED
#define RENDER_PIPELINE_PIPELINEENUMS_INCLUDED

#include <Reflection/macros.h>

namespace Engine {
    /**
     * @brief Utility definitions for graphics pipelines
     */
    namespace PipelineUtils {
        /**
         * @brief Filling mode of the rasterizer
         */
        enum class REFL_SER_CLASS() FillingMode {
            Fill, ///< Fill the polygon
            Line, ///< Draw only the lines of the polygon
            Point ///< Draw only the vertices of the polygon
        };

        /**
         * @brief Culling mode of the rasterizer
         */
        enum class REFL_SER_CLASS() CullingMode {
            None,  ///< No faces are culled
            Front, ///< Front faces are culled
            Back,  ///< Back faces are culled
            All    ///< All faces are culled
        };

        /**
         * @brief How front faces are determined via the winding of vertices
         */
        enum class REFL_SER_CLASS() FrontFace {
            Counterclockwise, ///< Counterclockwise faces are counted as front face
            Clockwise         ///< Clockwise faces are counted as front face
        };

        /**
         * @brief Comparator used for depth test etc.
         * @see https://registry.khronos.org/vulkan/specs/latest/man/html/VkCompareOp.html
         */
        enum class REFL_SER_CLASS() DSComparator {
            Never,
            Less,
            Equal,
            LEqual,
            Greater,
            NEqual,
            GEqual,
            Always
        };

        /**
         * @brief Stencil operations
         * @see https://registry.khronos.org/vulkan/specs/latest/man/html/VkStencilOp.html
         */
        enum class REFL_SER_CLASS() StencilOperation {
            Keep,
            Zero,
            Replace,
            IncrClamp,
            DecrClamp,
            Invert,
            IncrWrap,
            DecrWrap
        };

        /**
         * @brief Mask for color channels for attachments
         * @see https://docs.vulkan.org/refpages/latest/refpages/source/VkColorComponentFlagBits.html
         */
        enum class ColorChannelMask : int {
            None = 0x0,

            R = 0x1,
            G = 0x2,
            B = 0x4,
            A = 0x8,

            RG = R | G,
            RB = R | B,
            RA = R | A,
            GB = G | B,
            GA = G | A,
            BA = B | A,
            RGB = R | G | B,
            RGA = R | G | A,
            RBA = R | B | A,
            GBA = G | B | A,
            RGBA = R | G | B | A,

            All = RGBA
        };

        /**
         * @brief Blending operations
         * @see https://registry.khronos.org/vulkan/specs/latest/man/html/VkBlendOp.html
         */
        enum class REFL_SER_CLASS() BlendOperation {
            None,
            Add,
            Substract,
            ReverseSubstract,
            Min,
            Max
        };

        /**
         * @brief Blending factors
         * @see https://registry.khronos.org/vulkan/specs/latest/man/html/VkBlendFactor.html
         */
        enum class REFL_SER_CLASS() BlendFactor {
            Zero,
            One,
            SrcColor,
            OneMinusSrcColor,
            DstColor,
            OneMinusDstColor,
            SrcAlpha,
            OneMinusSrcAlpha,
            DstAlpha,
            OneMinusDstAlpha
        };
    } // namespace PipelineUtils
} // namespace Engine

#endif // RENDER_PIPELINE_PIPELINEENUMS_INCLUDED
