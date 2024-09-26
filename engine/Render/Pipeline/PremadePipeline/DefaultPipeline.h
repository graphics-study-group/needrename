#ifndef RENDER_PIPELINE_PREMADEPIPELINE_DEFAULTPIPELINE_INCLUDED
#define RENDER_PIPELINE_PREMADEPIPELINE_DEFAULTPIPELINE_INCLUDED

#include "Render/Pipeline/Pipeline.h"

namespace Engine {
    namespace PremadePipeline {
        class DefaultPipeline : public Pipeline {
        public:
            [[deprecated("Use ConfigurablePipeline for flexibility.")]]
            DefaultPipeline(std::weak_ptr <RenderSystem> system);

            void CreatePipeline(Subpass subpass, 
                const PipelineLayout & layout, 
                const std::vector<std::reference_wrapper<const ShaderModule>> & shaders
                ) override;
        protected:
            static vk::PipelineRasterizationStateCreateInfo CreateRasterizationState();
            static vk::PipelineMultisampleStateCreateInfo CreateMultisampleState();
            static vk::PipelineDepthStencilStateCreateInfo CreateDepthStencilState();
            static vk::PipelineColorBlendAttachmentState CreateColorBlendAttachmentState();
            static vk::PipelineColorBlendStateCreateInfo CreateColorBlendState(const std::vector<vk::PipelineColorBlendAttachmentState> & attachments);
        };
    };
};

#endif // RENDER_PIPELINE_PREMADEPIPELINE_DEFAULTPIPELINE_INCLUDED
