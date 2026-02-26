#include "MaterialTemplate.h"

#include "Asset/AssetRef.h"
#include "Asset/Material/MaterialTemplateAsset.h"

#include "MaterialInstance.h"
#include "Render/AttachmentUtilsFunc.h"
#include "Render/DebugUtils.h"
#include "Render/ImageUtilsFunc.h"
#include "Render/Pipeline/PipelineInfo.h"
#include "Render/Pipeline/PipelineUtils.hpp"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/Swapchain.h"
#include "Render/RenderSystem/CameraManager.h"

#include "Render/Memory/ShaderParameters/ShaderParameterLayout.h"

#include <SDL3/SDL.h>
#include <fstream>
#include <glm.hpp>
#include <vulkan/vulkan.hpp>

namespace Engine {

    struct MaterialTemplate::impl {
        constexpr static std::array<vk::DynamicState, 2> PIPELINE_DYNAMIC_STATES = {
            vk::DynamicState::eViewport, vk::DynamicState::eScissor
        };

        vk::DescriptorPool desc_pool {};
        vk::PipelineLayout pipeline_layout {};
        const ShdrRfl::SPLayout * m_layout{};

        vk::UniquePipeline pipeline {};
        std::string m_name{};

        void CreatePipeline(
            RenderSystem & system,
            const std::vector <vk::ShaderModule> shader_modules,
            const MaterialTemplateSinglePassProperties &prop,
            VertexAttribute attribute
        ) {
            vk::Device device = system.GetDevice();

            // Process shaders.
            vk::SpecializationInfo speci{};
            std::vector <vk::PipelineShaderStageCreateInfo> psscis;
            std::vector <vk::SpecializationMapEntry> sme;
            std::vector <std::byte> specialization_constant_buffer;
            {
                // Prepare specialization constants
                speci = PipelineUtils::FillSpecializationInfo(
                    prop.shaders.specialization_constants,
                    sme, specialization_constant_buffer
                );

                psscis.resize(prop.shaders.shaders.size());
                for (size_t i = 0; i < prop.shaders.shaders.size(); i++) {
                    auto shader_asset = prop.shaders.shaders[i].cas<ShaderAsset>();
                    psscis[i] = vk::PipelineShaderStageCreateInfo{
                        {},
                        PipelineUtils::ToVulkanShaderStageFlagBits(shader_asset->shaderType),
                        shader_modules[i],
                        shader_asset->m_entry_point.empty() ? "main" : shader_asset->m_entry_point.c_str(),
                        &speci
                    };
                }
            }

            bool use_swapchain_attachments =
                prop.attachments.color.empty() && prop.attachments.depth == ImageUtils::ImageFormat::UNDEFINED;

            auto vertex_bindings = attribute.ToVkVertexInputBinding();
            auto vertex_attribute = attribute.ToVkVertexAttribute();
            auto vis = vk::PipelineVertexInputStateCreateInfo{
                vk::PipelineVertexInputStateCreateFlags{},
                vertex_bindings,
                vertex_attribute
            };
            auto iasi = vk::PipelineInputAssemblyStateCreateInfo{{}, vk::PrimitiveTopology::eTriangleList, vk::False};
            auto vsi = vk::PipelineViewportStateCreateInfo{{}, 1, nullptr, 1, nullptr};
            auto rsci = PipelineUtils::ToVulkanRasterizationStateCreateInfo(prop.rasterizer);
            auto msi = vk::PipelineMultisampleStateCreateInfo{{}, vk::SampleCountFlagBits::e1, vk::False};
            auto dsci = PipelineUtils::ToVulkanDepthStencilStateCreateInfo(prop.depth_stencil);
            auto dsi = vk::PipelineDynamicStateCreateInfo{{}, PIPELINE_DYNAMIC_STATES};

            vk::PipelineColorBlendStateCreateInfo cbsi{};
            vk::PipelineRenderingCreateInfo prci{};
            std::vector<vk::PipelineColorBlendAttachmentState> cbass;

            vk::Format default_color_format{system.GetSwapchain().GetColorFormat()};

            std::vector<vk::Format> color_attachment_formats{prop.attachments.color.size(), vk::Format::eUndefined};
            // Fill in attachment information
            if (use_swapchain_attachments) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_RENDER,
                    "Material template \"%s\" does not specify its color attachment format. "
                    "Falling back to Swapchain default format. Gamma correction and color space may be incorrect.",
                    m_name.c_str()
                );

                color_attachment_formats = {default_color_format};

                prci = vk::PipelineRenderingCreateInfo{
                    0, color_attachment_formats, vk::Format::eUndefined, vk::Format::eUndefined
                };
                cbass.push_back(
                    vk::PipelineColorBlendAttachmentState{
                        vk::False,
                        vk::BlendFactor::eSrcAlpha,
                        vk::BlendFactor::eOneMinusSrcAlpha,
                        vk::BlendOp::eAdd,
                        vk::BlendFactor::eOne,
                        vk::BlendFactor::eZero,
                        vk::BlendOp::eAdd,
                        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB
                            | vk::ColorComponentFlagBits::eA
                    }
                );
            } else if (prop.attachments.color.empty() && prop.attachments.depth != ImageUtils::ImageFormat::UNDEFINED) {
                // Has depth attachment with no color attachments => depth-only
                prci = vk::PipelineRenderingCreateInfo{
                    0, 0, nullptr, ImageUtils::GetVkFormat(prop.attachments.depth), vk::Format::eUndefined, nullptr
                };
            } else {
                // All custom attachments
                assert(
                    prop.attachments.color.size() == prop.attachments.color_blending.size()
                    && "Mismatched color attachment and blending operation size."
                );

                cbass = PipelineUtils::ToVulkanColorBlendingOps(prop.attachments.color_blending);
                color_attachment_formats = PipelineUtils::ToVulkanFormat(prop.attachments.color, default_color_format);

                prci = vk::PipelineRenderingCreateInfo{
                    0,
                    color_attachment_formats,
                    ImageUtils::GetVkFormat(prop.attachments.depth),
                    vk::Format::eUndefined
                };
            }

            cbsi.logicOpEnable = vk::False;
            cbsi.setAttachments(cbass);

            vk::GraphicsPipelineCreateInfo gpci{};
            gpci.setStages(psscis);
            gpci.pVertexInputState = &vis;
            gpci.pInputAssemblyState = &iasi;
            gpci.pViewportState = &vsi;
            gpci.pRasterizationState = &rsci;
            gpci.pMultisampleState = &msi;
            gpci.pDepthStencilState = &dsci;
            gpci.pColorBlendState = &cbsi;
            gpci.pDynamicState = &dsi;
            gpci.layout = pipeline_layout;
            gpci.renderPass = nullptr;
            gpci.subpass = 0;
            gpci.pNext = &prci;

            auto ret = device.createGraphicsPipelineUnique(nullptr, gpci);
            pipeline = std::move(ret.value);
            SDL_LogInfo(
                SDL_LOG_CATEGORY_RENDER, "Successfully created material %s.", m_name.c_str()
            );
        }
    };

    MaterialTemplate::MaterialTemplate(RenderSystem &system) : m_system(system), pimpl(std::make_unique<impl>()) {
    }

    MaterialTemplate::MaterialTemplate(
        RenderSystem &system,
        const MaterialTemplateSinglePassProperties &properties,
        const std::vector<vk::ShaderModule> &shaders,
        vk::PipelineLayout layout,
        vk::DescriptorPool pool,
        const ShdrRfl::SPLayout * reflected,
        VertexAttribute attribute,
        const std::string &name
    ) : MaterialTemplate(system) {

        pimpl->m_name = name;
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating pipelines for material %s.", pimpl->m_name.c_str());
        if (!pimpl->desc_pool) {
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "This material has no per-material data");
        }

        pimpl->desc_pool = pool;
        
        pimpl->pipeline_layout = layout;
        pimpl->m_layout = reflected;

        // Create pipelines
        pimpl->CreatePipeline(system, shaders, properties, attribute);
    }

    MaterialTemplate::~MaterialTemplate() = default;

    vk::Pipeline MaterialTemplate::GetPipeline() const noexcept {
        return pimpl->pipeline.get();
    }
    vk::PipelineLayout MaterialTemplate::GetPipelineLayout() const noexcept {
        return pimpl->pipeline_layout;
    }

    vk::DescriptorPool MaterialTemplate::GetDescriptorPool() const noexcept {
        return pimpl->desc_pool;
    }

    const ShdrRfl::SPLayout &MaterialTemplate::GetReflectedShaderInfo() const noexcept {
        return *(pimpl->m_layout);
    }
    bool MaterialTemplate::HasMaterialData() const noexcept {
        return pimpl->desc_pool;
    }
} // namespace Engine
