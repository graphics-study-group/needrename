#include "ConfigurablePipeline.h"

#include "Render/Pipeline/Shader.h"

namespace Engine::PremadePipeline {
    ConfigurablePipeline::ConfigurablePipeline(std::weak_ptr<RenderSystem> system) : Pipeline(system)
    {
    }

    void ConfigurablePipeline::SetPipelineConfiguration(
        const PipelineLayout &layout, 
        const std::vector<std::reference_wrapper<const ShaderModule>> &shaders
    ) {
        this->SetPipelineConfiguration(layout, shaders, {});
    }

    void ConfigurablePipeline::SetPipelineConfiguration(
        const PipelineLayout &layout, 
        const std::vector<std::reference_wrapper<const ShaderModule>> &shaders, 
        const PipelineConfig &config,
        bool skinned
    ) {
        assert(!shaders.empty());
        m_layout = layout.get();
        m_shaders = shaders;
        m_config = config;
        m_skinned = skinned;
    }

    void ConfigurablePipeline::CreatePipeline()
    {
        std::vector <vk::PipelineShaderStageCreateInfo> stages;
        stages.resize(m_shaders.size());
        for (size_t i = 0; i < m_shaders.size(); i++) {
            stages[i] = (m_shaders[i].get().GetStageCreateInfo());
        }
        
        vk::GraphicsPipelineCreateInfo info{};
        info.stageCount = stages.size();
        info.pStages = stages.data();

        auto vertex_input = GetVertexInputState(m_skinned);
        info.pVertexInputState = &vertex_input;

        auto input_assemb = GetInputAssemblyState();
        info.pInputAssemblyState = &input_assemb;

        auto viewport = GetViewportState();
        info.pViewportState = &viewport;

        auto raster = m_config.rasterization.ToVulkanRasterizationStateCreateInfo();
        info.pRasterizationState = &raster;

        auto multisample = CreateMultisampleState();
        info.pMultisampleState = &multisample;

        vk::PipelineDepthStencilStateCreateInfo depth_stencil{
            vk::PipelineDepthStencilStateCreateFlags{}, 
            vk::False, vk::False,
            vk::CompareOp::eLess,   // Lower => closer
            vk::False,
            vk::False,
            {}, {},
            0.0f, 1.0f
        };
        if (m_system.lock()->GetSwapchain().IsDepthEnabled()) {
            depth_stencil.depthTestEnable = vk::True;
            depth_stencil.depthWriteEnable = vk::True;
        }
        info.pDepthStencilState = &depth_stencil;

        std::vector<vk::PipelineColorBlendAttachmentState> attachment{ CreateColorBlendAttachmentState() };
        auto color_blend = CreateColorBlendState(attachment);
        info.pColorBlendState = &color_blend;

        auto dynamic = GetDynamicState();
        info.pDynamicState = &dynamic;

        info.layout = m_layout;

        // Dynamic rendering setup
        info.renderPass = nullptr;
        info.subpass = 0;

        // Get info from swapchain
        const auto & swapchain = m_system.lock()->GetSwapchain();
        vk::PipelineRenderingCreateInfo rinfo = swapchain.GetPipelineRenderingCreateInfo();
        info.pNext = &rinfo;

        auto ret = m_system.lock()->getDevice().createGraphicsPipelineUnique(nullptr, info);
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Pipeline creation successful with result %d.", static_cast<int>(ret.result));
        m_handle = std::move(ret.value);
    }

    vk::PipelineMultisampleStateCreateInfo
    ConfigurablePipeline::CreateMultisampleState()
    {
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
    ConfigurablePipeline::CreateDepthStencilState() {
    return vk::PipelineDepthStencilStateCreateInfo();
    }

    vk::PipelineColorBlendAttachmentState
    ConfigurablePipeline::CreateColorBlendAttachmentState() {
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

    vk::PipelineColorBlendStateCreateInfo 
    ConfigurablePipeline::CreateColorBlendState(
        const std::vector<vk::PipelineColorBlendAttachmentState> & attachments
    ) {
        assert(!attachments.empty());

        vk::PipelineColorBlendStateCreateInfo info{};
        info.attachmentCount = attachments.size();
        info.pAttachments = attachments.data();
        info.logicOpEnable = vk::False;
        return info;
    }
};
