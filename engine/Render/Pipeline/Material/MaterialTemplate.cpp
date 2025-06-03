#include "MaterialTemplate.h"
#include "MaterialInstance.h"
#include "Asset/AssetRef.h"
#include "Render/RenderSystem.h"
#include "Render/Pipeline/PipelineUtils.h"
#include "Render/ConstantData/PerModelConstants.h"
#include "Render/Renderer/HomogeneousMesh.h"
#include "Render/RenderSystem/GlobalConstantDescriptorPool.h"
#include "Render/RenderSystem/Swapchain.h"
#include "Render/Pipeline/Material/ShaderUtils.h"
#include "Render/DebugUtils.h"

#include <glm.hpp>
#include <fstream>
#include <SDL3/SDL.h>

namespace Engine
{
    void MaterialTemplate::CreatePipeline(
        uint32_t pass_index, 
        const MaterialTemplateSinglePassProperties &prop,
        vk::Device device)
    {
        PassInfo pass_info;

        // Process and reflect on shaders.
        std::vector <ShaderUtils::ReflectedDataCollection> reflected;
        std::vector <vk::PipelineShaderStageCreateInfo> psscis;
        pass_info.shaders.resize(prop.shaders.shaders.size());
        psscis.resize(prop.shaders.shaders.size());
        reflected.resize(prop.shaders.shaders.size());
        for (size_t i = 0; i < prop.shaders.shaders.size(); i++) {
            assert(prop.shaders.shaders[i] && "Invalid shader asset.");

            auto shader_asset = prop.shaders.shaders[i]->cas<ShaderAsset>();
            auto code = shader_asset->binary;
            reflected[i] = ShaderUtils::ReflectSpirvData(code);
            vk::ShaderModuleCreateInfo ci {
                {},
                code.size() * sizeof(uint32_t),
                reinterpret_cast<const uint32_t *> (code.data())
            };
            
            pass_info.shaders[i] = device.createShaderModuleUnique(ci);
            DEBUG_SET_NAME_TEMPLATE(device, pass_info.shaders[i].get(), std::format("Shader Module - {}", shader_asset->m_name));

            psscis[i] = vk::PipelineShaderStageCreateInfo {
                {},
                PipelineUtils::ToVulkanShaderStageFlagBits(shader_asset->shaderType),
                pass_info.shaders[i].get(),
                "main"
            };
        }

        // Save uniform locations
        #ifndef NDEBUG
        std::unordered_set <std::string> names;
        for (auto & ref : reflected) {
            for (auto & [name, _] : ref.inblock.names) {
                if (names.find(name) != names.end()) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Duplicated variable name %s", name.c_str());
                }
                names.insert(name);
            }
            for (auto & [name, _] : ref.desc.names) {
                if (names.find(name) != names.end()) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Duplicated variable name %s", name.c_str());
                }
                names.insert(name);
            }
        }
        #endif
        for (auto & ref : reflected) {
            for (auto & [_, idx] : ref.inblock.names) {
                idx += pass_info.inblock.vars.size();
            }
            pass_info.inblock.names.merge(ref.inblock.names);
            pass_info.inblock.vars.insert(pass_info.inblock.vars.end(), ref.inblock.vars.begin(), ref.inblock.vars.end());

            for (auto & [_, idx] : ref.desc.names) {
                idx += pass_info.inblock.vars.size();
            }
            pass_info.desc.names.merge(ref.desc.names);
            pass_info.desc.vars.insert(pass_info.desc.vars.end(), ref.desc.vars.begin(), ref.desc.vars.end());
        }

        pass_info.inblock.maximal_ubo_size = 0;
        for (const auto & var : pass_info.inblock.vars) {
            pass_info.inblock.maximal_ubo_size = std::max(
                pass_info.inblock.maximal_ubo_size, 
                1ULL * var.inblock_location.offset + var.inblock_location.size
            );
        }

        // Create pipeline layout
        {
            const auto & pool = m_system.lock()->GetGlobalConstantDescriptorPool();

            const std::vector <vk::DescriptorSetLayoutBinding> * desc_bindings {nullptr};
            // auto desc_bindings = PipelineUtils::ToVulkanDescriptorSetLayoutBindings(prop.shaders);
            for (const auto & ref : reflected) {
                if (ref.has_material_descriptor_set) {
                    if (desc_bindings != nullptr) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Multiple material descriptors found. Only one is used.");
                    }
                    desc_bindings = &ref.per_material_descriptor_set_layout.bindings;
                }
            }
            if (desc_bindings) {
                vk::DescriptorSetLayoutCreateInfo dslci {
                    {}, *desc_bindings
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
            } else {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Material %s pipeline %u has no material descriptors.", this->m_name.c_str(), pass_index);

                std::array <vk::PushConstantRange, 1> push_constants{ConstantData::PerModelConstantPushConstant::GetPushConstantRange()};
                std::array <vk::DescriptorSetLayout, 2> set_layouts{
                    pool.GetPerSceneConstantLayout().get(), 
                    pool.GetPerCameraConstantLayout().get()
                };
                vk::PipelineLayoutCreateInfo plci{{}, set_layouts, push_constants};
                pass_info.pipeline_layout = device.createPipelineLayoutUnique(plci);
            }
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
        DEBUG_SET_NAME_TEMPLATE(dvc, this->m_poolInfo.pool.get(), std::format("Desc Pool - Material {}", m_name));

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

        auto layout = m_passes.at(pass_index).desc_layout.get();
        if (!layout) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER, 
                "Allocating empty descriptor for material %s pass %u", 
                m_name.c_str(), pass_index
            );
            return nullptr;
        }

        vk::DescriptorSetAllocateInfo dsai {
            m_poolInfo.pool.get(), {layout}
        };
        auto sets = m_system.lock()->getDevice().allocateDescriptorSets(dsai);
        return sets[0];
    }
    std::variant<std::monostate, std::reference_wrapper<const MaterialTemplate::DescVar>, std::reference_wrapper<const MaterialTemplate::InblockVar>>
    MaterialTemplate::GetVariable(const std::string & name, uint32_t pass_index) const
    {
        auto idx = this->GetVariableIndex(name, pass_index);
        if (!idx)   return std::monostate{};
        if (idx.value().second) {
            return this->GetInBlockVariable(idx.value().first, pass_index);
        }
        return this->GetDescVariable(idx.value().first, pass_index);
    }
    const MaterialTemplate::DescVar &MaterialTemplate::GetDescVariable(uint32_t index, uint32_t pass_index) const
    {
        return this->m_passes.at(pass_index).desc.vars.at(index);
    }
    const MaterialTemplate::InblockVar &MaterialTemplate::GetInBlockVariable(uint32_t index, uint32_t pass_index) const
    {
        return this->m_passes.at(pass_index).inblock.vars.at(index);
    }
    std::optional<std::pair<uint32_t, bool>> MaterialTemplate::GetVariableIndex(const std::string &name, uint32_t pass_index) const noexcept
    {
        auto pitr = m_passes.find(pass_index);
        assert(pitr != m_passes.end() && "Invaild pass index");
        auto itr = pitr->second.inblock.names.find(name);
        if (itr == pitr->second.inblock.names.end()) {
            auto nitr = pitr->second.desc.names.find(name);
            return nitr == pitr->second.desc.names.end() ? 
                std::nullopt : 
                std::optional<std::pair<uint32_t, bool>>{std::make_pair(nitr->second, false)};
        }
        return std::make_pair(itr->second, true);
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
        return m_passes.at(pass_index).inblock.maximal_ubo_size;
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
