#include "MaterialTemplate.h"
#include "MaterialInstance.h"
#include "Asset/AssetRef.h"
#include "Render/RenderSystem.h"
#include "Render/Pipeline/PipelineUtils.h"
#include "Render/ConstantData/PerModelConstants.h"
#include "Render/Renderer/HomogeneousMesh.h"
#include "Render/RenderSystem/GlobalConstantDescriptorPool.h"
#include "Render/RenderSystem/Swapchain.h"

#include <glm.hpp>
#include <fstream>
#include <SDL3/SDL.h>

namespace Engine
{
    void MaterialTemplate::CreatePipeline(
        uint32_t pass_index, 
        const MaterialTemplateSinglePassProperties &prop,
        vk::Device device
    )
    {
        PassInfo pass_info;

        // Create pipeline layout
        {
            const auto & pool = m_system.lock()->GetGlobalConstantDescriptorPool();

            auto desc_bindings = PipelineUtils::ToVulkanDescriptorSetLayoutBindings(prop.shaders);
            vk::DescriptorSetLayoutCreateInfo dslci {
                {}, desc_bindings
            };
            pass_info.desc_layout = device.createDescriptorSetLayoutUnique(dslci);

            std::array <vk::PushConstantRange, 1> push_constants{ConstantData::PerModelConstantPushConstant::GetPushConstantRange()};
            std::array <vk::DescriptorSetLayout, 3> set_layouts{
                pool.GetPerSceneConstantLayout().get(), 
                pool.GetPerCameraConstantLayout().get(),
                pass_info.desc_layout.get()
            };

            vk::PipelineLayoutCreateInfo plci{{}, set_layouts, push_constants};
            pass_info.pipeline_layout = device.createPipelineLayoutUnique(plci);
        }

        // Create shaders
        std::vector <vk::PipelineShaderStageCreateInfo> psscis;
        pass_info.shaders.resize(prop.shaders.shaders.size());
        psscis.resize(prop.shaders.shaders.size());
        for (size_t i = 0; i < prop.shaders.shaders.size(); i++) {
            assert(prop.shaders.shaders[i] && "Invalid shader asset.");
            auto shader_asset = prop.shaders.shaders[i]->cas<ShaderAsset>();
            auto code = shader_asset->binary;
            vk::ShaderModuleCreateInfo ci {
                {},
                code.size() * sizeof(uint32_t),
                reinterpret_cast<const uint32_t *> (code.data())
            };
            pass_info.shaders[i] = device.createShaderModuleUnique(ci);
            psscis[i] = vk::PipelineShaderStageCreateInfo {
                {},
                PipelineUtils::ToVulkanShaderStageFlagBits(shader_asset->shaderType),
                pass_info.shaders[i].get(),
                "main"
            };
        }

        bool use_swapchain_attachments = prop.attachments.color.empty() && prop.attachments.depth == ImageUtils::ImageFormat::UNDEFINED;

        auto vis = HomogeneousMesh::GetVertexInputState();
        auto iasi = vk::PipelineInputAssemblyStateCreateInfo {{}, vk::PrimitiveTopology::eTriangleList, vk::False};
        auto vsi = vk::PipelineViewportStateCreateInfo {{}, 1, nullptr, 1, nullptr};
        auto rsci = PipelineUtils::ToVulkanRasterizationStateCreateInfo(prop.rasterizer);
        auto msi = vk::PipelineMultisampleStateCreateInfo{{}, vk::SampleCountFlagBits::e1, vk::False};
        auto dsci = PipelineUtils::ToVulkanDepthStencilStateCreateInfo(prop.depth_stencil);
        auto dsi = vk::PipelineDynamicStateCreateInfo{{}, PassInfo::PIPELINE_DYNAMIC_STATES};
        
        vk::PipelineColorBlendStateCreateInfo cbsi {};
        vk::PipelineRenderingCreateInfo prci {};
        std::vector<vk::PipelineColorBlendAttachmentState> cbass;

        vk::Format default_color_format {m_system.lock()->GetSwapchain().GetImageFormat().format};
        vk::Format default_depth_format {ImageUtils::GetVkFormat(m_system.lock()->GetSwapchain().DEPTH_FORMAT)};
        AttachmentUtils::AttachmentOp default_color_op{
            vk::AttachmentLoadOp::eClear, 
            vk::AttachmentStoreOp::eStore,
            vk::ClearValue{vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f}}
        };
        AttachmentUtils::AttachmentOp default_depth_op{
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eDontCare,
            vk::ClearValue{vk::ClearDepthStencilValue{1.0f, 0u}}
        };

        // Fill in attachment information
        if (use_swapchain_attachments) {
            prci = vk::PipelineRenderingCreateInfo {
                0, {default_color_format}, default_depth_format, vk::Format::eUndefined
            };
            cbass.push_back(
                vk::PipelineColorBlendAttachmentState{
                    vk::False,
                    vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                    vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
                    vk::ColorComponentFlagBits::eR | 
                    vk::ColorComponentFlagBits::eG |
                    vk::ColorComponentFlagBits::eB |
                    vk::ColorComponentFlagBits::eA
                }
            );

            pass_info.attachments.color_attachment_ops.push_back(
                prop.attachments.color_ops.empty() ? default_color_op : prop.attachments.color_ops[0]
            );
            pass_info.attachments.ds_attachment_ops = default_depth_op;

        } else if (prop.attachments.color.empty() && prop.attachments.depth != ImageUtils::ImageFormat::UNDEFINED) {
            // Has depth attachment with no color attachments => depth-only
            prci = vk::PipelineRenderingCreateInfo{
                0, 0, nullptr, ImageUtils::GetVkFormat(prop.attachments.depth), vk::Format::eUndefined, nullptr
            };
            pass_info.attachments.ds_attachment_ops = prop.attachments.ds_ops;
        } else {
            // All custom attachments
            // XXX: This case is not thoroughly tested!
            std::vector <vk::Format> color_attachment_formats {prop.attachments.color.size(), vk::Format::eUndefined};
            pass_info.attachments.color_attachment_ops.resize(prop.attachments.color_ops.size());
    
            assert(prop.attachments.color.size() == prop.attachments.color_ops.size() && "Mismatched color attachment and operation size.");
            for (size_t i = 0; i < prop.attachments.color.size(); i++) {
                color_attachment_formats[i] = (
                    prop.attachments.color[i] == ImageUtils::ImageFormat::UNDEFINED ?
                    default_color_format :
                    ImageUtils::GetVkFormat(prop.attachments.color[i])
                );
                pass_info.attachments.color_attachment_ops[i] = prop.attachments.color_ops[i];
            }
            pass_info.attachments.ds_attachment_ops = prop.attachments.ds_ops;

            prci = vk::PipelineRenderingCreateInfo{
                0, 
                color_attachment_formats,
                prop.attachments.depth == ImageUtils::ImageFormat::UNDEFINED ? default_depth_format : ImageUtils::GetVkFormat(prop.attachments.depth),
                vk::Format::eUndefined
            };
        }
        cbsi.logicOpEnable = vk::False;
        cbsi.setAttachments(cbass);

        vk::GraphicsPipelineCreateInfo gpci {};
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
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Successfully created pass %u for material %s.", pass_index, m_name.c_str());

        // Save uniform locations
        pass_info.uniforms.maximal_ubo_size = 0ull;
        for (const auto & uniform : prop.shaders.uniforms) {
            if (pass_info.uniforms.name_mapping.find(uniform.name) != pass_info.uniforms.name_mapping.end()) {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Found duplicated uniform name %s.", uniform.name.c_str());
            }
            pass_info.uniforms.name_mapping[uniform.name] = pass_info.uniforms.variables.size();
            pass_info.uniforms.variables.push_back(
                ShaderVariable{
                    .type = uniform.type, 
                    .location = ShaderVariable::Location{
                        .set = static_cast<uint32_t>(uniform.frequency),
                        .binding = uniform.binding, 
                        .offset = uniform.offset
                    }
                }
            );

            if(uniform.frequency == ShaderVariableProperty::Frequency::PerMaterial) {
                pass_info.uniforms.maximal_ubo_size = std::max(
                    pass_info.uniforms.maximal_ubo_size,
                    uniform.offset + ShaderVariableProperty::SizeOf(uniform.type)
                );
            }
        }

        this->m_passes[pass_index] = std::move(pass_info);
    }

    void MaterialTemplate::CreatePipelines(const MaterialTemplateProperties &props)
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Createing pipelines for material %s.", m_name.c_str());
        // Prepare descriptor pool
        vk::DescriptorPoolCreateInfo dpci {
            {},
            PoolInfo::MAX_SET_SIZE,
            PoolInfo::DESCRIPTOR_POOL_SIZES,
            nullptr
        };
        vk::Device dvc = m_system.lock()->getDevice();
        this->m_poolInfo.pool = dvc.createDescriptorPoolUnique(dpci);

        // Create a fallback point-clamp sampler
        vk::SamplerCreateInfo sci {};
        sci.magFilter = sci.minFilter = vk::Filter::eNearest;
        sci.addressModeU = sci.addressModeV = sci.addressModeW = vk::SamplerAddressMode::eRepeat;
        m_default_sampler = dvc.createSamplerUnique(sci);

        // Create pipelines
        for (const auto & [key, val] : props.properties) {
            this->CreatePipeline(key, val, dvc);
        }
    }
    MaterialTemplate::MaterialTemplate(
        std::weak_ptr<RenderSystem> system, 
        std::shared_ptr<AssetRef> asset
        ) : m_system(system), m_asset(asset)
    {
        assert(asset);
        auto mtasset = asset->as<MaterialTemplateAsset>();

        m_name = mtasset->name;
        CreatePipelines(mtasset->properties);
    }
    std::shared_ptr<MaterialInstance> MaterialTemplate::CreateInstance()
    {
        return std::make_shared<MaterialInstance>(m_system, shared_from_this());
    }
    vk::Pipeline MaterialTemplate::GetPipeline(uint32_t pass_index) const noexcept
    {
        assert(m_passes.contains(pass_index) && "Invaild pass index");
        return m_passes.at(pass_index).pipeline.get();
    }
    vk::PipelineLayout MaterialTemplate::GetPipelineLayout(uint32_t pass_index) const noexcept
    {
        assert(m_passes.contains(pass_index) && "Invaild pass index");
        return m_passes.at(pass_index).pipeline_layout.get();
    }
    auto MaterialTemplate::GetAllPassInfo() const noexcept -> const decltype(m_passes) &
    {
        return m_passes;
    }
    auto MaterialTemplate::GetPassInfo(uint32_t pass_index) const -> const PassInfo &
    {
        return m_passes.at(pass_index);
    }
    vk::DescriptorSetLayout MaterialTemplate::GetDescriptorSetLayout(uint32_t pass_index) const
    {
        assert(m_passes.contains(pass_index) && "Invaild pass index");
        return m_passes.at(pass_index).desc_layout.get();
    }
    vk::DescriptorSet MaterialTemplate::AllocateDescriptorSet(uint32_t pass_index)
    {
        assert(m_passes.contains(pass_index) && "Invaild pass index");
        vk::DescriptorSetAllocateInfo dsai {
            m_poolInfo.pool.get(), {m_passes.at(pass_index).desc_layout.get()}
        };
        auto sets = m_system.lock()->getDevice().allocateDescriptorSets(dsai);
        return sets[0];
    }
    std::optional<std::reference_wrapper<const MaterialTemplate::ShaderVariable>>
    MaterialTemplate::GetVariable(const std::string &name, uint32_t pass_index) const
    {
        auto idx = this->GetVariableIndex(name, pass_index);
        if (!idx)   return std::nullopt;
        return this->GetVariable(idx.value(), pass_index);
    }
    const MaterialTemplate::ShaderVariable &MaterialTemplate::GetVariable(uint32_t index, uint32_t pass_index) const
    {
        return this->m_passes.at(pass_index).uniforms.variables.at(index);
    }
    std::optional<uint32_t> MaterialTemplate::GetVariableIndex(const std::string &name, uint32_t pass_index) const noexcept
    {
        auto pitr = m_passes.find(pass_index);
        assert(pitr != m_passes.end() && "Invaild pass index");
        auto itr = pitr->second.uniforms.name_mapping.find(name);
        if (itr == pitr->second.uniforms.name_mapping.end())
            return std::nullopt;
        return itr->second;
    }
    AttachmentUtils::AttachmentOp MaterialTemplate::GetDSAttachmentOperation(uint32_t pass_index) const
    {
        assert(m_passes.contains(pass_index) && "Invaild pass index");
        return m_passes.at(pass_index).attachments.ds_attachment_ops;
    }
    AttachmentUtils::AttachmentOp MaterialTemplate::GetColorAttachmentOperation(uint32_t index, uint32_t pass_index) const
    {
        assert(m_passes.contains(pass_index) && "Invaild pass index");
        return m_passes.at(pass_index).attachments.color_attachment_ops.at(index);
    }
    uint64_t MaterialTemplate::GetMaximalUBOSize(uint32_t pass_index) const
    {
        assert(m_passes.contains(pass_index) && "Invaild pass index");
        return m_passes.at(pass_index).uniforms.maximal_ubo_size;
    }
    void MaterialTemplate::PlaceUBOVariables(const MaterialInstance &instance, std::vector<std::byte> & memory, uint32_t pass_index) const
    {
        const auto & variables = instance.GetVariables(pass_index);
        const auto & pass_info = this->GetPassInfo(pass_index);

        PipelineInfo::PlaceUBOVariables(variables, pass_info, memory);
    }
    std::vector<std::pair<uint32_t, vk::DescriptorImageInfo>> 
    MaterialTemplate::GetDescriptorImageInfo(const MaterialInstance &instance, uint32_t pass_index) const
    {
        const auto & pass_info = this->GetPassInfo(pass_index);
        const auto & instance_vars = instance.GetVariables(pass_index);
        return PipelineInfo::GetDescriptorImageInfo(instance_vars, pass_info, m_default_sampler.get());
    }
} // namespace Engine
