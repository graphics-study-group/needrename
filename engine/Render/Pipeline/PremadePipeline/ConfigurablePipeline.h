#ifndef PIPELINE_PREMADEPIPELINE_CONFIGURABLEPIPELINE_INCLUDED
#define PIPELINE_PREMADEPIPELINE_CONFIGURABLEPIPELINE_INCLUDED

#include "Render/Pipeline/Pipeline.h"
#include "Render/Pipeline/PremadePipeline/PipelineConfigurationStruct.h"

namespace Engine {
    namespace PremadePipeline {
        /// @brief A basic configurable rasterization pipeline with color and depth attachments created according to the swapchain
        class ConfigurablePipeline : public Pipeline {
        public:
            ConfigurablePipeline(std::weak_ptr <RenderSystem> system);
            
            virtual void SetPipelineConfiguration(
                const PipelineLayout & layout, 
                const std::vector<std::reference_wrapper<const ShaderModule>> & shaders
            ) override;

            /// @brief Set the configuration of the pipeline.
            /// @param layout constant data (descriptors and push constants) layout of the pipeline.
            /// @param shaders vector of shader modules. It should at least contain a fragment shader and a vertex shader.
            /// @param config configuration for fixed-function stages.
            void SetPipelineConfiguration(
                const PipelineLayout & layout, 
                const std::vector<std::reference_wrapper<const ShaderModule>> & shaders,
                const PipelineConfig & config,
                bool skinned = false
            );

            virtual void CreatePipeline() override;
        protected:

            // TODO: we urgently need better lifetime management for vk types
            vk::PipelineLayout m_layout{};
            std::vector<std::reference_wrapper<const ShaderModule>> m_shaders {};
            PipelineConfig m_config{};
            bool m_skinned{false};

            static vk::PipelineMultisampleStateCreateInfo CreateMultisampleState();
            static vk::PipelineDepthStencilStateCreateInfo CreateDepthStencilState();
            static vk::PipelineColorBlendAttachmentState CreateColorBlendAttachmentState();
            static vk::PipelineColorBlendStateCreateInfo CreateColorBlendState(const std::vector<vk::PipelineColorBlendAttachmentState> & attachments);
        };
    };
};

#endif // PIPELINE_PREMADEPIPELINE_CONFIGURABLEPIPELINE_INCLUDED
