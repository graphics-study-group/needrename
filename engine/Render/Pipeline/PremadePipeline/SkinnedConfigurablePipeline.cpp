#include "SkinnedConfigurablePipeline.h"

#include "Render/Pipeline/Shader.h"
#include "Render/Renderer/SkinnedHomogeneousMesh.h"

Engine::SkinnedConfigurablePipeline::SkinnedConfigurablePipeline(std::weak_ptr<RenderSystem> system) : ConfigurablePipeline(system)
{
}

void Engine::SkinnedConfigurablePipeline::CreatePipeline()
{
    std::vector <vk::PipelineShaderStageCreateInfo> stages;
    stages.resize(m_shaders.size());
    for (size_t i = 0; i < m_shaders.size(); i++) {
        stages[i] = (m_shaders[i].get().GetStageCreateInfo());
    }
    
    vk::GraphicsPipelineCreateInfo info{};
    info.stageCount = stages.size();
    info.pStages = stages.data();

    auto vertex_input = SkinnedHomogeneousMesh::GetVertexInputState();
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
