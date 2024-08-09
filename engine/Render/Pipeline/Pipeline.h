#ifndef RENDER_PIPELINE_PIPELINE_INCLUDED
#define RENDER_PIPELINE_PIPELINE_INCLUDED

#include <memory>
#include <vulkan/vulkan.hpp>
#include "Render/RenderSystem.h"

namespace Engine{
    class Pipeline {
    public:

        Pipeline (std::weak_ptr <RenderSystem> system);

        void CreatePipelineLayout(const std::vector<vk::DescriptorSetLayout> & set, const std::vector<vk::PushConstantRange> & push);
        void CreateRenderPass(const std::vector<vk::AttachmentDescription> & attachment, const std::vector<vk::SubpassDescription> & subpass);
        void CreatePipeline(const std::vector<vk::PipelineShaderStageCreateInfo> & stage);

    protected:

        vk::PipelineDynamicStateCreateInfo CreateDynamicState();
        vk::PipelineVertexInputStateCreateInfo CreateVertexInputState();
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

        std::weak_ptr <RenderSystem> m_system;
        vk::UniqueRenderPass m_pass {};
        vk::UniquePipelineLayout m_layout {};
        vk::UniquePipeline m_pipeline {};
    };
}

#endif // RENDER_PIPELINE_PIPELINE_INCLUDED
