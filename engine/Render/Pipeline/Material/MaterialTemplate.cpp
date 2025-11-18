#include "MaterialTemplate.h"

#include "Asset/AssetRef.h"
#include "Asset/Material/MaterialTemplateAsset.h"

#include "MaterialInstance.h"
#include "Render/AttachmentUtilsFunc.h"
#include "Render/ConstantData/PerModelConstants.h"
#include "Render/DebugUtils.h"
#include "Render/ImageUtilsFunc.h"
#include "Render/Pipeline/PipelineInfo.h"
#include "Render/Pipeline/PipelineUtils.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/GlobalConstantDescriptorPool.h"
#include "Render/RenderSystem/Swapchain.h"
#include "Render/Renderer/HomogeneousMesh.h"

#include "Render/Memory/ShaderParameters/ShaderParameterLayout.h"

#include <SDL3/SDL.h>
#include <fstream>
#include <glm.hpp>
#include <vulkan/vulkan.hpp>

namespace Engine {

    struct MaterialTemplate::impl {
        PassInfo m_passes{};
        ShdrRfl::SPLayout m_layout{};

        PoolInfo m_poolInfo{};
        std::string m_name{};
    };

    void MaterialTemplate::CreatePipeline(
        const MaterialTemplateSinglePassProperties &prop,
        vk::Device device
    ) {
        PassInfo pass_info;
        pimpl->m_layout.variables.clear();
        pimpl->m_layout.interfaces.clear();
        pimpl->m_layout.name_mapping.clear();

        // Process and reflect on shaders.
        std::vector<vk::PipelineShaderStageCreateInfo> psscis;
        pass_info.shaders.resize(prop.shaders.shaders.size());
        psscis.resize(prop.shaders.shaders.size());
        for (size_t i = 0; i < prop.shaders.shaders.size(); i++) {
            assert(prop.shaders.shaders[i] && "Invalid shader asset.");

            auto shader_asset = prop.shaders.shaders[i]->cas<ShaderAsset>();
            auto code = shader_asset->binary;
            pimpl->m_layout.Merge(Engine::ShdrRfl::SPLayout::Reflect(code, true));
            vk::ShaderModuleCreateInfo ci{
                {}, code.size() * sizeof(uint32_t), reinterpret_cast<const uint32_t *>(code.data())
            };

            pass_info.shaders[i] = device.createShaderModuleUnique(ci);
            DEBUG_SET_NAME_TEMPLATE(
                device, pass_info.shaders[i].get(), std::format("Shader Module - {}", shader_asset->m_name)
            );

            psscis[i] = vk::PipelineShaderStageCreateInfo{
                {},
                PipelineUtils::ToVulkanShaderStageFlagBits(shader_asset->shaderType),
                pass_info.shaders[i].get(),
                shader_asset->m_entry_point.empty() ? "main" : shader_asset->m_entry_point.c_str()
            };
        }

        // Create pipeline layout
        {
            const auto &pool = m_system.GetGlobalConstantDescriptorPool();
            auto desc_bindings = pimpl->m_layout.GenerateLayoutBindings(2);
            if (!desc_bindings.empty()) {
                unsigned ubo_count{0};
                for (auto & db : desc_bindings) {
                    if (db.descriptorType == vk::DescriptorType::eUniformBuffer) {
                        ubo_count ++;
                    }
                }
                if (ubo_count != 1) {
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_RENDER,
                        "Found %u uniform buffer binding points instead of one.",
                        ubo_count
                    );
                }

                vk::DescriptorSetLayoutCreateInfo dslci{{}, desc_bindings};
                pass_info.desc_layout = device.createDescriptorSetLayoutUnique(dslci);

                std::array<vk::PushConstantRange, 1> push_constants{
                    ConstantData::PerModelConstantPushConstant::GetPushConstantRange()
                };
                std::array<vk::DescriptorSetLayout, 3> set_layouts{
                    pool.GetPerSceneConstantLayout().get(),
                    pool.GetPerCameraConstantLayout().get(),
                    pass_info.desc_layout.get()
                };
                vk::PipelineLayoutCreateInfo plci{{}, set_layouts, push_constants};
                pass_info.pipeline_layout = device.createPipelineLayoutUnique(plci);
            } else {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_RENDER,
                    "Material %s pipeline has no material descriptors.",
                    pimpl->m_name.c_str()
                );

                std::array<vk::PushConstantRange, 1> push_constants{
                    ConstantData::PerModelConstantPushConstant::GetPushConstantRange()
                };
                std::array<vk::DescriptorSetLayout, 2> set_layouts{
                    pool.GetPerSceneConstantLayout().get(), pool.GetPerCameraConstantLayout().get()
                };
                vk::PipelineLayoutCreateInfo plci{{}, set_layouts, push_constants};
                pass_info.pipeline_layout = device.createPipelineLayoutUnique(plci);
            }
        }

        bool use_swapchain_attachments =
            prop.attachments.color.empty() && prop.attachments.depth == ImageUtils::ImageFormat::UNDEFINED;

        auto vis = HomogeneousMesh::GetVertexInputState();
        auto iasi = vk::PipelineInputAssemblyStateCreateInfo{{}, vk::PrimitiveTopology::eTriangleList, vk::False};
        auto vsi = vk::PipelineViewportStateCreateInfo{{}, 1, nullptr, 1, nullptr};
        auto rsci = PipelineUtils::ToVulkanRasterizationStateCreateInfo(prop.rasterizer);
        auto msi = vk::PipelineMultisampleStateCreateInfo{{}, vk::SampleCountFlagBits::e1, vk::False};
        auto dsci = PipelineUtils::ToVulkanDepthStencilStateCreateInfo(prop.depth_stencil);
        auto dsi = vk::PipelineDynamicStateCreateInfo{{}, PassInfo::PIPELINE_DYNAMIC_STATES};

        vk::PipelineColorBlendStateCreateInfo cbsi{};
        vk::PipelineRenderingCreateInfo prci{};
        std::vector<vk::PipelineColorBlendAttachmentState> cbass;

        vk::Format default_color_format{m_system.GetSwapchain().COLOR_FORMAT_VK};
        vk::Format default_depth_format{m_system.GetSwapchain().DEPTH_FORMAT_VK};

        std::vector<vk::Format> color_attachment_formats{prop.attachments.color.size(), vk::Format::eUndefined};
        // Fill in attachment information
        if (use_swapchain_attachments) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER,
                "Material template \"%s\" does not specify its color attachment format. "
                "Falling back to Swapchain default format. Gamma correction and color space may be incorrect.",
                this->pimpl->m_name.c_str()
            );

            color_attachment_formats = {default_color_format};

            prci = vk::PipelineRenderingCreateInfo{
                0, color_attachment_formats, default_depth_format, vk::Format::eUndefined
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

            cbass.resize(prop.attachments.color_blending.size());

            for (size_t i = 0; i < prop.attachments.color.size(); i++) {
                if (prop.attachments.color[i] == ImageUtils::ImageFormat::UNDEFINED) {
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_RENDER,
                        "Material template \"%s\" (color attachment %llu) does not specify its format. "
                        "Falling back to Swapchain default format. Gamma correction and color space may be incorrect.",
                        this->pimpl->m_name.c_str(),
                        i
                    );
                    color_attachment_formats[i] = default_color_format;
                } else {
                    color_attachment_formats[i] = ImageUtils::GetVkFormat(prop.attachments.color[i]);
                }

                const auto &cb = prop.attachments.color_blending[i];
                if (cb.color_op == PipelineUtils::BlendOperation::None
                    || cb.alpha_op == PipelineUtils::BlendOperation::None) {
                    cbass[i] = vk::PipelineColorBlendAttachmentState{
                        vk::False,
                        vk::BlendFactor::eSrcAlpha,
                        vk::BlendFactor::eOneMinusSrcAlpha,
                        vk::BlendOp::eAdd,
                        vk::BlendFactor::eOne,
                        vk::BlendFactor::eZero,
                        vk::BlendOp::eAdd,
                        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB
                            | vk::ColorComponentFlagBits::eA
                    };
                } else {
                    cbass[i] = vk::PipelineColorBlendAttachmentState{
                        vk::True,
                        PipelineUtils::ToVkBlendFactor(cb.src_color),
                        PipelineUtils::ToVkBlendFactor(cb.dst_color),
                        PipelineUtils::ToVkBlendOp(cb.color_op),
                        PipelineUtils::ToVkBlendFactor(cb.src_alpha),
                        PipelineUtils::ToVkBlendFactor(cb.dst_alpha),
                        PipelineUtils::ToVkBlendOp(cb.alpha_op),
                        static_cast<vk::ColorComponentFlags>(static_cast<int>(cb.color_write_mask))
                    };
                }
            }

            prci = vk::PipelineRenderingCreateInfo{
                0,
                color_attachment_formats,
                prop.attachments.depth == ImageUtils::ImageFormat::UNDEFINED
                    ? default_depth_format
                    : ImageUtils::GetVkFormat(prop.attachments.depth),
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
        gpci.layout = pass_info.pipeline_layout.get();
        gpci.renderPass = nullptr;
        gpci.subpass = 0;
        gpci.pNext = &prci;

        auto ret = device.createGraphicsPipelineUnique(nullptr, gpci);
        pass_info.pipeline = std::move(ret.value);
        SDL_LogInfo(
            SDL_LOG_CATEGORY_RENDER, "Successfully created material %s.", pimpl->m_name.c_str()
        );

        pimpl->m_passes = std::move(pass_info);
    }

    MaterialTemplate::MaterialTemplate(RenderSystem &system) : m_system(system), pimpl(std::make_unique<impl>()) {
    }

    void MaterialTemplate::Instantiate(const MaterialTemplateAsset &asset) {
        pimpl->m_name = asset.name;
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Createing pipelines for material %s.", pimpl->m_name.c_str());
        // Prepare descriptor pool
        vk::DescriptorPoolCreateInfo dpci{{}, PoolInfo::MAX_SET_SIZE, PoolInfo::DESCRIPTOR_POOL_SIZES, nullptr};
        vk::Device dvc = m_system.GetDevice();
        pimpl->m_poolInfo.pool = dvc.createDescriptorPoolUnique(dpci);
        DEBUG_SET_NAME_TEMPLATE(
            dvc, pimpl->m_poolInfo.pool.get(), std::format("Descriptor Pool - Material {}", pimpl->m_name)
        );

        // Create pipelines
        this->CreatePipeline(asset.properties, dvc);
    }

    MaterialTemplate::~MaterialTemplate() = default;

    vk::Pipeline MaterialTemplate::GetPipeline() const noexcept {
        return pimpl->m_passes.pipeline.get();
    }
    vk::PipelineLayout MaterialTemplate::GetPipelineLayout() const noexcept {
        return pimpl->m_passes.pipeline_layout.get();
    }
    auto MaterialTemplate::GetPassInfo() const -> const PassInfo & {
        return pimpl->m_passes;
    }
    vk::DescriptorSetLayout MaterialTemplate::GetDescriptorSetLayout() const {
        return pimpl->m_passes.desc_layout.get();
    }
    std::vector<vk::DescriptorSet> MaterialTemplate::AllocateDescriptorSets(uint32_t size) {

        auto layout = pimpl->m_passes.desc_layout.get();
        if (!layout) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER,
                "Allocating empty descriptor for material %s",
                pimpl->m_name.c_str()
            );
            return std::vector<vk::DescriptorSet>(size, nullptr);
        }

        std::vector layouts(size, layout);
        vk::DescriptorSetAllocateInfo dsai{pimpl->m_poolInfo.pool.get(), layouts};
        auto sets = m_system.GetDevice().allocateDescriptorSets(dsai);
        assert(sets.size() == size);
        return sets;
    }
    const ShdrRfl::SPVariable * MaterialTemplate::GetVariable(const std::string & name) const noexcept
    {
        if (auto itr{pimpl->m_layout.name_mapping.find(name)}; 
            itr != pimpl->m_layout.name_mapping.end()) {
            return itr->second;
        }
        return nullptr;
    }
    const ShdrRfl::SPLayout &MaterialTemplate::GetReflectedShaderInfo() const noexcept {
        return pimpl->m_layout;
    }
    size_t MaterialTemplate::GetExpectedUniformBufferSize() const noexcept {
        size_t size = 0;
        for (auto pinterface : pimpl->m_layout.interfaces) {
            if (auto pbuf = dynamic_cast<const ShdrRfl::SPInterfaceBuffer *>(pinterface)) {
                if (pbuf->type == ShdrRfl::SPInterfaceBuffer::Type::UniformBuffer) {
                    if (auto ptype = dynamic_cast<const ShdrRfl::SPTypeSimpleStruct *>(pbuf->underlying_type)) {
                        size = std::max(size, ptype->expected_size);
                    } 
                }
            }
        }
        return size;
    }
} // namespace Engine
