#include "Pipeline.h"

namespace Engine
{
    Pipeline::Pipeline(std::weak_ptr<RenderSystem> system) : VkWrapper(system) {}

    void Pipeline::CreatePipeline(Subpass subpass,
        const PipelineLayout & layout,
        const std::vector<vk::PipelineShaderStageCreateInfo> & stage)
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating pipeline.");
        assert(!stage.empty());

        vk::GraphicsPipelineCreateInfo info{};
        info.stageCount = stage.size();
        info.pStages = stage.data();

        auto vertex_input = CreateVertexInputState();
        info.pVertexInputState = &vertex_input;

        auto input_assemb = CreateInputAssembly();
        info.pInputAssemblyState = &input_assemb;

        auto viewport = CreateViewportState();
        info.pViewportState = &viewport;

        auto raster = CreateRasterizationState();
        info.pRasterizationState = &raster;

        auto multisample = CreateMultisampleState();
        info.pMultisampleState = &multisample;

        info.pDepthStencilState = nullptr;

        std::vector<vk::PipelineColorBlendAttachmentState> attachment{ CreateColorBlendAttachmentState() };
        auto color_blend = CreateColorBlendState(attachment);
        info.pColorBlendState = &color_blend;

        auto dynamic = CreateDynamicState();
        info.pDynamicState = &dynamic;

        info.layout = layout.get();
        info.renderPass = subpass.pass;
        info.subpass = subpass.index;

        auto ret = m_system.lock()->getDevice().createGraphicsPipelineUnique(nullptr, info);
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Pipeline creation successful with result %d.", static_cast<int>(ret.result));
        m_handle = std::move(ret.value);
    }


    vk::PipelineDynamicStateCreateInfo Pipeline::CreateDynamicState() {
        vk::PipelineDynamicStateCreateInfo info{};
        info.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
        info.pDynamicStates = dynamic_states.data();
        return info;
    }

    vk::PipelineVertexInputStateCreateInfo Pipeline::CreateVertexInputState() {
        vk::PipelineVertexInputStateCreateInfo info{};
        info.vertexBindingDescriptionCount = 0;
        info.pVertexBindingDescriptions = nullptr;
        info.vertexAttributeDescriptionCount = 0;
        info.pVertexAttributeDescriptions = nullptr;
        return info;
    }

    vk::PipelineInputAssemblyStateCreateInfo Pipeline::CreateInputAssembly() {
        vk::PipelineInputAssemblyStateCreateInfo info{};
        info.topology = vk::PrimitiveTopology::eTriangleList;
        info.primitiveRestartEnable = vk::False;
        return info;
    }

    vk::PipelineViewportStateCreateInfo Pipeline::CreateViewportState() {
        vk::PipelineViewportStateCreateInfo info{};
        info.viewportCount = 1;
        info.scissorCount = 1;
        return info;
    }

    vk::PipelineRasterizationStateCreateInfo
    Pipeline::CreateRasterizationState() {
        vk::PipelineRasterizationStateCreateInfo info{};
        info.depthClampEnable = vk::False;
        info.rasterizerDiscardEnable = vk::False;
        info.polygonMode = vk::PolygonMode::eFill;
        info.lineWidth = 1.0f;
        info.cullMode = vk::CullModeFlagBits::eBack;
        info.frontFace = vk::FrontFace::eClockwise;
        info.depthBiasEnable = vk::False;
        return info;
    }

    vk::PipelineMultisampleStateCreateInfo Pipeline::CreateMultisampleState() {
        vk::PipelineMultisampleStateCreateInfo info{};
        info.sampleShadingEnable = vk::False;
        info.rasterizationSamples = vk::SampleCountFlagBits::e1;
        info.minSampleShading = 1.0f;
        info.pSampleMask = nullptr;
        info.alphaToCoverageEnable = vk::False;
        info.alphaToOneEnable = vk::False;
        return info;
    }

    vk::PipelineDepthStencilStateCreateInfo
    Pipeline::CreateDepthStencilState() {
      return vk::PipelineDepthStencilStateCreateInfo();
    }

    vk::PipelineColorBlendAttachmentState
    Pipeline::CreateColorBlendAttachmentState() {
        vk::PipelineColorBlendAttachmentState state{};
        state.colorWriteMask = 
            vk::ColorComponentFlagBits::eR |
            vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB |
            vk::ColorComponentFlagBits::eA;
        state.blendEnable = vk::False;
        state.srcColorBlendFactor = vk::BlendFactor::eOne;
        state.dstColorBlendFactor = vk::BlendFactor::eZero;
        state.colorBlendOp = vk::BlendOp::eAdd;
        state.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        state.dstAlphaBlendFactor = vk::BlendFactor::eZero;
        state.alphaBlendOp = vk::BlendOp::eAdd;

        return state;
    }

    vk::PipelineColorBlendStateCreateInfo Pipeline::CreateColorBlendState(
        const std::vector<vk::PipelineColorBlendAttachmentState> & attachments
        ) {
        assert(!attachments.empty());

        vk::PipelineColorBlendStateCreateInfo info{};
        info.attachmentCount = attachments.size();
        info.pAttachments = attachments.data();
        info.logicOpEnable = vk::False;
        return info;
    }

} // namespace Engine

