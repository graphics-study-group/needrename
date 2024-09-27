#ifndef PIPELINE_PREMADEPIPELINE_CONFIGURABLEPIPELINE_INCLUDED
#define PIPELINE_PREMADEPIPELINE_CONFIGURABLEPIPELINE_INCLUDED

#include "Render/Pipeline/Pipeline.h"
#include "Render/Pipeline/PremadePipeline/PipelineConfigurationStruct.h"

namespace Engine {
    namespace PremadePipeline {
        class ConfigurablePipeline : public Pipeline {
        public:
            ConfigurablePipeline(std::weak_ptr <RenderSystem> system);

            /* void CreatePipeline(Subpass subpass, 
                const PipelineLayout & layout, 
                const std::vector<std::reference_wrapper<const ShaderModule>> & shaders
                ) override;

            void CreatePipeline(Subpass subpass, 
                const PipelineLayout & layout, 
                const std::vector<std::reference_wrapper<const ShaderModule>> & shaders,
                const PipelineConfig & config
                ); */
            
            virtual void SetPipelineConfiguration(
                const PipelineLayout & layout, 
                const std::vector<std::reference_wrapper<const ShaderModule>> & shaders
            ) override;
            void SetPipelineConfiguration(
                const PipelineLayout & layout, 
                const std::vector<std::reference_wrapper<const ShaderModule>> & shaders,
                const PipelineConfig & config
            );

            virtual void CreatePipeline(Subpass subpass) override;
        protected:

            // TODO: we urgently need better lifetime management for vk types
            vk::PipelineLayout m_layout{};
            std::vector<std::reference_wrapper<const ShaderModule>> m_shaders;
            PipelineConfig m_config{};

            static vk::PipelineMultisampleStateCreateInfo CreateMultisampleState();
            static vk::PipelineDepthStencilStateCreateInfo CreateDepthStencilState();
            static vk::PipelineColorBlendAttachmentState CreateColorBlendAttachmentState();
            static vk::PipelineColorBlendStateCreateInfo CreateColorBlendState(const std::vector<vk::PipelineColorBlendAttachmentState> & attachments);
        };
    };
};

#endif // PIPELINE_PREMADEPIPELINE_CONFIGURABLEPIPELINE_INCLUDED
