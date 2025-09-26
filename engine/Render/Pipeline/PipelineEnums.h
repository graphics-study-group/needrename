#ifndef RENDER_PIPELINE_PIPELINEENUMS_INCLUDED
#define RENDER_PIPELINE_PIPELINEENUMS_INCLUDED

#include <Render/Enums/enum_def.h>

namespace Engine {
    namespace PipelineUtils {

        using FillingMode = _enum::FillingMode;
        using CullingMode = _enum::CullingMode;
        /// @brief C.f. https://registry.khronos.org/vulkan/specs/latest/man/html/VkCompareOp.html
        using DSComparator = _enum::Comparator;
        /// @brief C.f. https://registry.khronos.org/vulkan/specs/latest/man/html/VkStencilOp.html
        using StencilOperation = _enum::StencilOperation;
        /// @brief C.f. https://registry.khronos.org/vulkan/specs/latest/man/html/VkBlendOp.html
        using BlendOperation = _enum::BlendOperation;
        /// @brief C.f. https://registry.khronos.org/vulkan/specs/latest/man/html/VkBlendFactor.html
        using BlendFactor = _enum::BlendFactor;

        using FrontFace = _enum::FrontFace;

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
    } // namespace PipelineUtils
} // namespace Engine

#endif // RENDER_PIPELINE_PIPELINEENUMS_INCLUDED
