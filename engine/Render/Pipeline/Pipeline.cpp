#include "Pipeline.h"

namespace Engine
{
    Pipeline::Pipeline(std::weak_ptr<RenderSystem> system) : m_system(system) {}

    void Pipeline::CreatePipeline(const std::vector<vk::PipelineShaderStageCreateInfo> & stage) {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating pipeline.");
        assert(m_layout);
        assert(m_pass);
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

        info.layout = m_layout.get();
        info.renderPass = m_pass.get();
        info.subpass = 0;

        auto ret = m_system.lock()->getDevice().createGraphicsPipelineUnique(nullptr, info);
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Pipeline creation successful with result %d.", static_cast<int>(ret.result));
        m_pipeline = std::move(ret.value);
    }

    void Pipeline::CreatePipelineLayout(
        const std::vector<vk::DescriptorSetLayout>& set,
        const std::vector<vk::PushConstantRange>& push) {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating pipeline layout.");
        vk::PipelineLayoutCreateInfo info{};
        info.setLayoutCount = set.size();
        info.pSetLayouts = set.empty() ? nullptr : set.data(); 
        info.pushConstantRangeCount = push.size();
        info.pPushConstantRanges = push.empty() ? nullptr : push.data();
        m_layout = m_system.lock()->getDevice().createPipelineLayoutUnique(info);
    }

    void Pipeline::CreateRenderPass(
        const std::vector<vk::AttachmentDescription>& attachment,
        const std::vector<vk::SubpassDescription>& subpass) {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating render pass.");

        assert(!attachment.empty());
        assert(!subpass.empty());

        vk::RenderPassCreateInfo info{};
        info.attachmentCount = attachment.size();
        info.pAttachments = attachment.data();
        info.subpassCount = subpass.size();
        info.pSubpasses = subpass.data();
        m_pass = m_system.lock()->getDevice().createRenderPassUnique(info);
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

