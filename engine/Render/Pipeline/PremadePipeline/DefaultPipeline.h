#ifndef RENDER_PIPELINE_PREMADEPIPELINE_DEFAULTPIPELINE_INCLUDED
#define RENDER_PIPELINE_PREMADEPIPELINE_DEFAULTPIPELINE_INCLUDED

#include "Render/Pipeline/Pipeline.h"

namespace Engine {
    namespace PremadePipeline {
        class DefaultPipeline : public Pipeline {
        public:
            [[deprecated("Use ConfigurablePipeline for flexibility.")]]
            DefaultPipeline(std::weak_ptr <RenderSystem> system);

            virtual void SetPipelineConfiguration(
                const PipelineLayout & layout, 
                const std::vector<std::reference_wrapper<const ShaderModule>> & shaders
            ) override;
            virtual void CreatePipeline(Subpass subpass) override;

        protected:
            // TODO: we urgently need better lifetime management for vk types
            vk::PipelineLayout m_layout{};
            std::vector<std::reference_wrapper<const ShaderModule>> m_shaders;

            static vk::PipelineRasterizationStateCreateInfo CreateRasterizationState();
            static vk::PipelineMultisampleStateCreateInfo CreateMultisampleState();
            static vk::PipelineDepthStencilStateCreateInfo CreateDepthStencilState();
            static vk::PipelineColorBlendAttachmentState CreateColorBlendAttachmentState();
            static vk::PipelineColorBlendStateCreateInfo CreateColorBlendState(const std::vector<vk::PipelineColorBlendAttachmentState> & attachments);
        };
    };
};

#endif // RENDER_PIPELINE_PREMADEPIPELINE_DEFAULTPIPELINE_INCLUDED
