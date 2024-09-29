#ifndef PIPELINE_PREMADEPIPELINE_PIPELINECONFIGURATIONSTRUCT_INCLUDED
#define PIPELINE_PREMADEPIPELINE_PIPELINECONFIGURATIONSTRUCT_INCLUDED

#include <vulkan/vulkan.hpp>

namespace Engine {
    struct RasterizationConfig {
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

        static vk::PolygonMode ToVkPolygonMode(FillingMode mode);
        static vk::CullModeFlags ToVkCullMode(CullingMode mode);
        static vk::FrontFace ToVkFrontFace(FrontFace face);

        FillingMode filling{FillingMode::Fill};
        float line_width{1.0f};

        CullingMode culling{CullingMode::None};

        FrontFace front{FrontFace::Counterclockwise};

        vk::PipelineRasterizationStateCreateInfo
        ToVulkanRasterizationStateCreateInfo() const;
    };

    struct PipelineConfig {
        RasterizationConfig rasterization{};
    };
}

#endif // PIPELINE_PREMADEPIPELINE_PIPELINECONFIGURATIONSTRUCT_INCLUDED
