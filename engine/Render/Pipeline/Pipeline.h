#ifndef RENDER_PIPELINE_PIPELINE_INCLUDED
#define RENDER_PIPELINE_PIPELINE_INCLUDED

#include <memory>
#include <vulkan/vulkan.hpp>
#include "Render/RenderSystem.h"
#include "Render/Pipeline/RenderPass.h"
#include "Render/Pipeline/PipelineLayout.h"

namespace Engine{
    class Pipeline : public VkWrapper<vk::UniquePipeline> {
    public:

        Pipeline (std::weak_ptr <RenderSystem> system);

        void CreatePipeline(
            Subpass subpass, 
            const PipelineLayout & layout, 
            const std::vector<vk::PipelineShaderStageCreateInfo> & stage
        );

    protected:

        vk::PipelineDynamicStateCreateInfo CreateDynamicState();
        vk::PipelineInputAssemblyStateCreateInfo CreateInputAssembly();
        vk::PipelineViewportStateCreateInfo CreateViewportState();
        vk::PipelineRasterizationStateCreateInfo CreateRasterizationState();
        vk::PipelineMultisampleStateCreateInfo CreateMultisampleState();
        vk::PipelineDepthStencilStateCreateInfo CreateDepthStencilState();
        vk::PipelineColorBlendAttachmentState CreateColorBlendAttachmentState();
        vk::PipelineColorBlendStateCreateInfo CreateColorBlendState(const std::vector<vk::PipelineColorBlendAttachmentState> & attachments);

        constexpr static std::array<vk::DynamicState, 2> dynamic_states = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };
    };
}

#endif // RENDER_PIPELINE_PIPELINE_INCLUDED
