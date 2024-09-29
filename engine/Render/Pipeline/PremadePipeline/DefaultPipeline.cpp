#include "DefaultPipeline.h"

#include "Render/Pipeline/Shader.h"

namespace Engine {
    namespace PremadePipeline {
        DefaultPipeline::DefaultPipeline(std::weak_ptr<RenderSystem> system) : Pipeline(system) {
        }

        void DefaultPipeline::SetPipelineConfiguration(
            const PipelineLayout &layout, 
            const std::vector<std::reference_wrapper<const ShaderModule>> &shaders
        ) {
            assert(!shaders.empty());
            m_layout = layout.get();
            m_shaders = shaders;
        }

        void DefaultPipeline::CreatePipeline(
            Subpass subpass
        ) {
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating pipeline.");
            

            m_attached_subpass = subpass;

            std::vector <vk::PipelineShaderStageCreateInfo> stages;
            stages.resize(m_shaders.size());
            for (size_t i = 0; i < m_shaders.size(); i++) {
                stages[i] = (m_shaders[i].get().GetStageCreateInfo());
            }

            vk::GraphicsPipelineCreateInfo info{};
            info.stageCount = stages.size();
            info.pStages = stages.data();

            auto vertex_input = GetVertexInputState();
            info.pVertexInputState = &vertex_input;

            auto input_assemb = GetInputAssemblyState();
            info.pInputAssemblyState = &input_assemb;

            auto viewport = GetViewportState();
            info.pViewportState = &viewport;

            auto raster = CreateRasterizationState();
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
            info.renderPass = subpass.pass;
            info.subpass = subpass.index;

            auto ret = m_system.lock()->getDevice().createGraphicsPipelineUnique(nullptr, info);
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Pipeline creation successful with result %d.", static_cast<int>(ret.result));
            m_handle = std::move(ret.value);
        }

        vk::PipelineRasterizationStateCreateInfo
        DefaultPipeline::CreateRasterizationState() {
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

        vk::PipelineMultisampleStateCreateInfo DefaultPipeline::CreateMultisampleState() {
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
        DefaultPipeline::CreateDepthStencilState() {
        return vk::PipelineDepthStencilStateCreateInfo();
        }

        vk::PipelineColorBlendAttachmentState
        DefaultPipeline::CreateColorBlendAttachmentState() {
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

        vk::PipelineColorBlendStateCreateInfo DefaultPipeline::CreateColorBlendState(
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
};
