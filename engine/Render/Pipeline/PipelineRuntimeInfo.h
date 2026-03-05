#ifndef RENDER_PIPELINE_PIPELINERUNTIMEINFO_INCLUDED
#define RENDER_PIPELINE_PIPELINERUNTIMEINFO_INCLUDED

#include "Render/Renderer/VertexAttribute.h"
#include "Render/ImageUtils.h"

namespace Engine {
    
    /**
     * @brief Runtime information for graphics pipelines that is determined on
     * per draw basis.
     * 
     * Mainly includes mesh vertex attributes.
     */
    struct PipelineRuntimeInfoPerDraw {
        VertexAttribute va;

        bool operator== (const PipelineRuntimeInfoPerDraw &) const noexcept = default;
    };

    struct PipelineRuntimeInfoPerRenderingHeader {
        /**
         * How many samples are used for multisampling?
         * 0 and 1 are equivalent and always accepted.
         * Values that are not a power of two are not valid.
         * Other values (e.g. 8) may be supported depending on the platform.
         */
        uint8_t samples : 8;
        
        /**
         * Enable alpha-to-coverage technique for multisampling.
         */
        bool alpha_to_coverage_enable : 1;

        /**
         * Write one to alpha channel after multisampling.
         * It should be combined with alpha-to-coverage technique.
         */
        bool alpha_to_one_enable : 1;

        // Pad to 4 bytes
        uint8_t : 0;
        uint8_t _padding_1[2];

        bool operator== (const PipelineRuntimeInfoPerRenderingHeader & rhs) const noexcept {
            return (
                samples == rhs.samples &&
                alpha_to_coverage_enable == rhs.alpha_to_coverage_enable &&
                alpha_to_one_enable == rhs.alpha_to_one_enable
            );
        };
    };

    /**
     * @brief Runtime information for graphics pipelines that is determined on
     * per rendering pass basis.
     * 
     * Mainly includes attachment information and multisample counts.
     */
    struct PipelineRuntimeInfoPerRendering : PipelineRuntimeInfoPerRenderingHeader {
        /**
         * 
         * Color attachment format, terminated by UNDEFINED.
         */
        ImageUtils::ImageFormat color_attachment_format[8];
        /**
         * Depth and stencil attachment format.
         */
        ImageUtils::ImageFormat depth_stencil_attachment_format;

        bool operator== (const PipelineRuntimeInfoPerRendering & rhs) const noexcept {
            auto ret = static_cast<const PipelineRuntimeInfoPerRenderingHeader *>(this)->operator==(rhs);
            if (ret == false)   return false;
            if (depth_stencil_attachment_format != rhs.depth_stencil_attachment_format)
                return false;

            for (int i = 0; i < 8; i++) {
                if (color_attachment_format[i] != rhs.color_attachment_format[i])
                    return false;
                // Both undefined -> terminated.
                if (color_attachment_format[i] == ImageUtils::ImageFormat::UNDEFINED)
                    return true;
            }
            return true;
        };
    };

    /**
     * @brief Runtime information necessary to build a graphics pipeline.
     */
    struct PipelineRuntimeInfo : PipelineRuntimeInfoPerDraw, PipelineRuntimeInfoPerRendering {
        bool operator== (const PipelineRuntimeInfo &) const noexcept = default;
    };
}

#endif // RENDER_PIPELINE_PIPELINERUNTIMEINFO_INCLUDED
