#ifndef RENDER_PIPELINE_PIPELINEENUMS_INCLUDED
#define RENDER_PIPELINE_PIPELINEENUMS_INCLUDED

namespace Engine {
    namespace PipelineUtils{

        enum class FillingMode {
            Fill,
            Line,
            Point
        };
        enum class CullingMode {
            None,
            Front,
            Back,
            All
        };
        enum class FrontFace {
            Counterclockwise,
            Clockwise
        };

        /// @brief C.f. https://registry.khronos.org/vulkan/specs/latest/man/html/VkCompareOp.html
        enum class DSComparator {
            Never,
            Less,
            Equal,
            LEqual,
            Greater,
            NEqual,
            GEqual,
            Always
        };
        
        /// @brief C.f. https://registry.khronos.org/vulkan/specs/latest/man/html/VkStencilOp.html
        enum class StencilOperation {
            Keep,
            Zero,
            Replace,
            IncrClamp,
            DecrClamp,
            Invert,
            IncrWrap,
            DecrWrap
        };

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

        /// @brief C.f. https://registry.khronos.org/vulkan/specs/latest/man/html/VkBlendOp.html
        enum class BlendOperation {
            None,
            Add,
            Substract,
            ReverseSubstract,
            Min,
            Max
        };

        /// @brief C.f. https://registry.khronos.org/vulkan/specs/latest/man/html/VkBlendFactor.html
        enum class BlendFactor {
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
    }
}

#endif // RENDER_PIPELINE_PIPELINEENUMS_INCLUDED
