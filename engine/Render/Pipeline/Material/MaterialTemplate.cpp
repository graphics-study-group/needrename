#include "MaterialTemplate.h"

#include "Asset/AssetRef.h"
#include "Asset/Material/MaterialTemplateAsset.h"

#include "MaterialInstance.h"
#include "Render/AttachmentUtilsFunc.h"
#include "Render/DebugUtils.h"
#include "Render/ImageUtilsFunc.h"
#include "Render/Pipeline/PipelineRuntimeInfo.h"
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
            const PipelineRuntimeInfo &pri
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
                    auto shader_asset = prop.shaders.shaders[i]->cas<ShaderAsset>();
                    psscis[i] = vk::PipelineShaderStageCreateInfo{
                        {},
                        PipelineUtils::ToVulkanShaderStageFlagBits(shader_asset->shaderType),
                        shader_modules[i],
                        shader_asset->m_entry_point.empty() ? "main" : shader_asset->m_entry_point.c_str(),
                        &speci
                    };
                }
            }

            auto vertex_bindings = pri.va.ToVkVertexInputBinding();
            auto vertex_attribute = pri.va.ToVkVertexAttribute();
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

            std::vector<vk::Format> color_attachment_formats{};

            for (const auto & f : pri.color_attachment_format) {
                if (f == ImageUtils::ImageFormat::UNDEFINED)    break;
                color_attachment_formats.push_back(ImageUtils::GetVkFormat(f));
            }

            std::vector<vk::PipelineColorBlendAttachmentState> cbass{
                PipelineUtils::ToVulkanColorBlendingOps(prop.attachments.color_blending)
            };

            if (color_attachment_formats.size() < cbass.size()) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_RENDER,
                    "For material %s, %llu color render targets are requested, but only %llu are provided. "
                    "Shader writes to extra color attachments will be discarded.",
                    m_name.c_str(),
                    color_attachment_formats.size(),
                    cbass.size()
                );
                cbass.resize(color_attachment_formats.size());
            } else if (color_attachment_formats.size() > cbass.size()) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_RENDER,
                    "For material %s, %llu color render targets are requested, but only %llu are provided. "
                    "Extra color render targets will not be written in the shader.",
                    m_name.c_str(),
                    color_attachment_formats.size(),
                    cbass.size()
                );
                for (auto i = cbass.size(); i < color_attachment_formats.size(); i++) {
                    cbass.push_back(vk::PipelineColorBlendAttachmentState{});
                }
            }

            prci = vk::PipelineRenderingCreateInfo{
                0,
                color_attachment_formats,
                ImageUtils::GetVkFormat(pri.depth_stencil_attachment_format),
                // XXX: stencil attachment support
                vk::Format::eUndefined
            };
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
        const ShdrRfl::SPLayout &reflected,
        const PipelineRuntimeInfo &pri,
        const std::string &name
    ) : MaterialTemplate(system) {
        pimpl->m_name = name;
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating pipelines for material %s.", pimpl->m_name.c_str());
        if (!pimpl->desc_pool) {
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "This material has no per-material data");
        }

        pimpl->desc_pool = pool;
        
        pimpl->pipeline_layout = layout;
        pimpl->m_layout = &reflected;

        // Create pipelines
        pimpl->CreatePipeline(system, shaders, properties, pri);
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
