#include "MaterialTemplate.h"

#include "Asset/AssetRef.h"
#include "Render/Asset/Material/MaterialTemplateAsset.h"

#include "MaterialInstance.h"
#include "Render/Backend/Vulkan/AttachmentUtilsFunc.h"
#include "Render/Pipeline/PipelineRuntimeInfo.h"
#include "Render/Pipeline/PipelineUtils.hpp"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/CameraManager.h"
#include "Rhi/Backend/Vulkan/VulkanDebugUtils.h"
#include "Rhi/Backend/Vulkan/VulkanHandles.h"
#include "Rhi/Pipeline/ShaderParameterLayout.h"
#include "Rhi/Backend/Vulkan/VulkanImageUtils.h"
#include "Rhi/Backend/Vulkan/VulkanPipelineState.h"

#include <SDL3/SDL.h>
#include <fstream>
#include <glm.hpp>
#include <vulkan/vulkan.hpp>

namespace Engine {

    struct MaterialTemplate::impl {
        constexpr static std::array<vk::DynamicState, 2> PIPELINE_DYNAMIC_STATES = {
            vk::DynamicState::eViewport, vk::DynamicState::eScissor
        };

        vk::PipelineLayout pipeline_layout{};
        /// @brief Layout of the per-material set, the object the arena resolved.
        Rhi::DescriptorSetLayoutRef desc_set_layout{};
        const Rhi::SPLayout *m_layout{};
        /// @brief Whether the reflected layout has material data set. Computed once at
        /// construction, because `GenerateLayoutBindings` allocates.
        bool m_has_material_data{false};

        vk::UniquePipeline pipeline{};
        std::string m_name{};

        void CreatePipeline(
            RenderSystem &system,
            const std::vector<vk::ShaderModule> shader_modules,
            MaterialTemplateSinglePassProperties &prop,
            const PipelineRuntimeInfo &pri
        ) {
            vk::Device device = system.GetDevice();

            // Process shaders.
            vk::SpecializationInfo speci{};
            std::vector<vk::PipelineShaderStageCreateInfo> psscis;
            std::vector<vk::SpecializationMapEntry> sme;
            std::vector<std::byte> specialization_constant_buffer;
            {
                // Prepare specialization constants
                speci = PipelineUtils::FillSpecializationInfo(
                    prop.shaders.specialization_constants, sme, specialization_constant_buffer
                );

                psscis.resize(prop.shaders.shaders.size());
                for (size_t i = 0; i < prop.shaders.shaders.size(); i++) {
                    auto shader_asset = prop.shaders.shaders[i].as<ShaderAsset>();
                    psscis[i] = vk::PipelineShaderStageCreateInfo{
                        {},
                        PipelineUtils::ToVulkanShaderStageFlagBits(shader_asset->shaderType),
                        shader_modules[i],
                        shader_asset->m_entry_point.empty() ? "main" : shader_asset->m_entry_point.c_str(),
                        &speci
                    };
                }
            }

            // The four state groups, the vertex input and the attachment formats
            // are what a pipeline is. The viewport and the sample count are not
            // part of the description: one is dynamic and the other is a device
            // property.
            Rhi::PipelineDescription pipeline_description{};
            pipeline_description.vertex_bindings = pri.va.ToVertexInputBinding();
            pipeline_description.vertex_attributes = pri.va.ToVertexInputAttribute();
            pipeline_description.rasterization = Rhi::RasterizationState{
                prop.rasterizer.filling,
                prop.rasterizer.culling,
                prop.rasterizer.front,
                prop.rasterizer.line_width,
                prop.rasterizer.depth_bias_constant,
                prop.rasterizer.depth_bias_slope,
            };
            pipeline_description.depth_stencil = Rhi::DepthStencilState{
                prop.depth_stencil.depth_test_enable,
                prop.depth_stencil.depth_write_enable,
                prop.depth_stencil.depth_comparator,
                prop.depth_stencil.depth_bound_test_enable,
                prop.depth_stencil.min_depth,
                prop.depth_stencil.max_depth,
                prop.depth_stencil.stencil_test_enable,
                Rhi::StencilState{
                    prop.depth_stencil.stencil_front.fail_op,
                    prop.depth_stencil.stencil_front.pass_op,
                    prop.depth_stencil.stencil_front.zfail_op,
                    prop.depth_stencil.stencil_front.comparator,
                    prop.depth_stencil.stencil_front.compare_mask,
                    prop.depth_stencil.stencil_front.write_mask,
                    prop.depth_stencil.stencil_front.reference,
                },
                Rhi::StencilState{
                    prop.depth_stencil.stencil_back.fail_op,
                    prop.depth_stencil.stencil_back.pass_op,
                    prop.depth_stencil.stencil_back.zfail_op,
                    prop.depth_stencil.stencil_back.comparator,
                    prop.depth_stencil.stencil_back.compare_mask,
                    prop.depth_stencil.stencil_back.write_mask,
                    prop.depth_stencil.stencil_back.reference,
                },
            };
            for (const auto &blend : prop.attachments.color_blending) {
                // Either operation being `None` turns blending off, and the state
                // then resolves to the description default rather than to the
                // factors this entry names.
                if (blend.color_op == Rhi::BlendOperation::None || blend.alpha_op == Rhi::BlendOperation::None) {
                    // A target with no blending entry writes no channel at all,
                    // which is what a zero state has always meant here.
                    pipeline_description.color_blends.push_back(
                        Rhi::ColorBlendState{.write_mask = Rhi::ColorChannelMask::None}
                    );
                    continue;
                }
                pipeline_description.color_blends.push_back(
                    Rhi::ColorBlendState{
                        true,
                        blend.color_op,
                        blend.src_color,
                        blend.dst_color,
                        blend.alpha_op,
                        blend.src_alpha,
                        blend.dst_alpha,
                        blend.color_write_mask,
                    }
                );
            }
            for (const auto &f : pri.color_attachment_format) {
                if (f == Rhi::ImageFormat::UNDEFINED) break;
                pipeline_description.color_formats.push_back(f);
            }
            pipeline_description.depth_format = pri.depth_stencil_attachment_format;

            if (pipeline_description.color_formats.size() < pipeline_description.color_blends.size()) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_RENDER,
                    "For material %s, %llu color render targets are requested, but only %llu are provided. "
                    "Shader writes to extra color attachments will be discarded.",
                    m_name.c_str(),
                    pipeline_description.color_formats.size(),
                    pipeline_description.color_blends.size()
                );
                pipeline_description.color_blends.resize(pipeline_description.color_formats.size());
            } else if (pipeline_description.color_formats.size() > pipeline_description.color_blends.size()) {
                for (size_t i = pipeline_description.color_blends.size();
                     i < pipeline_description.color_formats.size();
                     i++) {
                    // A target with no blending entry writes no channel at all,
                    // which is what a zero state has always meant here.
                    pipeline_description.color_blends.push_back(
                        Rhi::ColorBlendState{.write_mask = Rhi::ColorChannelMask::None}
                    );
                }
            }

            const Rhi::VulkanPipelineState pipeline_state{pipeline_description};
            auto vsi = vk::PipelineViewportStateCreateInfo{{}, 1, nullptr, 1, nullptr};
            auto msi = vk::PipelineMultisampleStateCreateInfo{{}, vk::SampleCountFlagBits::e1, vk::False};
            auto dsi = vk::PipelineDynamicStateCreateInfo{{}, PIPELINE_DYNAMIC_STATES};

            vk::GraphicsPipelineCreateInfo gpci{};
            gpci.setStages(psscis);
            gpci.pNext = &pipeline_state.Rendering();
            gpci.pVertexInputState = &pipeline_state.VertexInput();
            gpci.pInputAssemblyState = &pipeline_state.InputAssembly();
            gpci.pViewportState = &vsi;
            gpci.pRasterizationState = &pipeline_state.Rasterization();
            gpci.pMultisampleState = &msi;
            gpci.pDepthStencilState = &pipeline_state.DepthStencil();
            gpci.pColorBlendState = &pipeline_state.ColorBlend();
            gpci.pDynamicState = &dsi;
            gpci.layout = pipeline_layout;
            gpci.renderPass = nullptr;
            gpci.subpass = 0;

            auto ret = device.createGraphicsPipelineUnique(nullptr, gpci);
            pipeline = std::move(ret.value);
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Successfully created material %s.", m_name.c_str());
        }
    };

    MaterialTemplate::MaterialTemplate(RenderSystem &system) : m_system(system), pimpl(std::make_unique<impl>()) {
    }

    MaterialTemplate::MaterialTemplate(
        RenderSystem &system,
        MaterialTemplateSinglePassProperties &properties,
        const std::vector<vk::ShaderModule> &shaders,
        vk::PipelineLayout layout,
        Rhi::DescriptorSetLayoutRef desc_set_layout,
        const Rhi::SPLayout &reflected,
        const PipelineRuntimeInfo &pri,
        const std::string &name
    ) : MaterialTemplate(system) {
        pimpl->m_name = name;
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating pipelines for material %s.", pimpl->m_name.c_str());

        pimpl->pipeline_layout = layout;
        pimpl->desc_set_layout = desc_set_layout;
        pimpl->m_layout = &reflected;
        // "Has per-material data" is "the reflected layout declares set 2".
        pimpl->m_has_material_data = !reflected.GenerateLayoutBindings(2, true, false).empty();
        if (!pimpl->m_has_material_data) {
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "This material has no per-material data");
        }

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

    vk::DescriptorSetLayout MaterialTemplate::GetDescriptorSetLayout() const noexcept {
        return Engine::Rhi::ToVulkanDescriptorSetLayout(pimpl->desc_set_layout.handle);
    }

    Rhi::DescriptorSetLayoutRef MaterialTemplate::GetDescriptorSetLayoutRef() const noexcept {
        return pimpl->desc_set_layout;
    }

    const Rhi::SPLayout &MaterialTemplate::GetReflectedShaderInfo() const noexcept {
        return *(pimpl->m_layout);
    }
    bool MaterialTemplate::HasMaterialData() const noexcept {
        return pimpl->m_has_material_data;
    }
} // namespace Engine








