#ifndef PIPELINE_PREMADEPIPELINE_CONFIGURABLEPIPELINE_INCLUDED
#define PIPELINE_PREMADEPIPELINE_CONFIGURABLEPIPELINE_INCLUDED

#include "Render/Pipeline/Pipeline.h"
#include "Render/Pipeline/PremadePipeline/PipelineConfigurationStruct.h"

namespace Engine {
    namespace PremadePipeline {
        class ConfigurablePipeline : public Pipeline {
        public:
            ConfigurablePipeline(std::weak_ptr <RenderSystem> system);

            void CreatePipeline(Subpass subpass, 
                const PipelineLayout & layout, 
                const std::vector<std::reference_wrapper<const ShaderModule>> & shaders
                ) override;

            void CreatePipeline(Subpass subpass, 
                const PipelineLayout & layout, 
                const std::vector<std::reference_wrapper<const ShaderModule>> & shaders,
                const PipelineConfig & config
                );
        protected:
            static vk::PipelineMultisampleStateCreateInfo CreateMultisampleState();
            static vk::PipelineDepthStencilStateCreateInfo CreateDepthStencilState();
            static vk::PipelineColorBlendAttachmentState CreateColorBlendAttachmentState();
            static vk::PipelineColorBlendStateCreateInfo CreateColorBlendState(const std::vector<vk::PipelineColorBlendAttachmentState> & attachments);
        };
    };
};

#endif // PIPELINE_PREMADEPIPELINE_CONFIGURABLEPIPELINE_INCLUDED
