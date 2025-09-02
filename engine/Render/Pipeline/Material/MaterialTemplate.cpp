#include "MaterialTemplate.h"

#include "Asset/AssetRef.h"
#include "Asset/Material/MaterialTemplateAsset.h"

#include "MaterialInstance.h"
#include "Render/AttachmentUtilsFunc.h"
#include "Render/ConstantData/PerModelConstants.h"
#include "Render/DebugUtils.h"
#include "Render/ImageUtilsFunc.h"
#include "Render/Pipeline/Material/ShaderUtils.h"
#include "Render/Pipeline/PipelineInfo.h"
#include "Render/Pipeline/PipelineUtils.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/GlobalConstantDescriptorPool.h"
#include "Render/RenderSystem/Swapchain.h"
#include "Render/Renderer/HomogeneousMesh.h"

#include <SDL3/SDL.h>
#include <fstream>
#include <glm.hpp>
#include <vulkan/vulkan.hpp>

namespace Engine {

    struct MaterialTemplate::impl {
        std::unordered_map<uint32_t, PassInfo> m_passes{};
        PoolInfo m_poolInfo{};
        vk::UniqueSampler m_default_sampler{};
        std::string m_name{};
    };

    void MaterialTemplate::CreatePipeline(
        uint32_t pass_index, const MaterialTemplateSinglePassProperties &prop, vk::Device device
    ) {
        PassInfo pass_info;

        // Process and reflect on shaders.
        std::vector<ShaderUtils::ReflectedDataCollection> reflected;
        std::vector<vk::PipelineShaderStageCreateInfo> psscis;
        pass_info.shaders.resize(prop.shaders.shaders.size());
        psscis.resize(prop.shaders.shaders.size());
        reflected.resize(prop.shaders.shaders.size());
        for (size_t i = 0; i < prop.shaders.shaders.size(); i++) {
            assert(prop.shaders.shaders[i] && "Invalid shader asset.");

            auto shader_asset = prop.shaders.shaders[i]->cas<ShaderAsset>();
            auto code = shader_asset->binary;
            reflected[i] = ShaderUtils::ReflectSpirvData(code);
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

// Save uniform locations
#ifndef NDEBUG
        std::unordered_set<std::string> names;
        for (auto &ref : reflected) {
            for (auto &[name, _] : ref.inblock.names) {
                if (names.find(name) != names.end()) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Duplicated variable name %s", name.c_str());
                }
                names.insert(name);
            }
            for (auto &[name, _] : ref.desc.names) {
                if (names.find(name) != names.end()) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Duplicated variable name %s", name.c_str());
                }
                names.insert(name);
            }
        }
#endif
        for (auto &ref : reflected) {
            for (auto &[_, idx] : ref.inblock.names) {
                idx += pass_info.inblock.vars.size();
            }
            pass_info.inblock.names.merge(ref.inblock.names);
            pass_info.inblock.vars.insert(
                pass_info.inblock.vars.end(), ref.inblock.vars.begin(), ref.inblock.vars.end()
            );

            for (auto &[_, idx] : ref.desc.names) {
                idx += pass_info.desc.vars.size();
            }
            pass_info.desc.names.merge(ref.desc.names);
            pass_info.desc.vars.insert(pass_info.desc.vars.end(), ref.desc.vars.begin(), ref.desc.vars.end());
        }

        pass_info.inblock.maximal_ubo_size = 0;
        for (const auto &var : pass_info.inblock.vars) {
            pass_info.inblock.maximal_ubo_size = std::max(
                pass_info.inblock.maximal_ubo_size, 1ULL * var.inblock_location.offset + var.inblock_location.size
            );
        }

        // Create pipeline layout
        {
            const auto &pool = m_system.GetGlobalConstantDescriptorPool();

            const std::vector<vk::DescriptorSetLayoutBinding> *desc_bindings{nullptr};
            // auto desc_bindings = PipelineUtils::ToVulkanDescriptorSetLayoutBindings(prop.shaders);
            for (const auto &ref : reflected) {
                if (ref.has_material_descriptor_set) {
                    if (desc_bindings != nullptr) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Multiple material descriptors found. Only one is used.");
                    }
                    desc_bindings = &ref.per_material_descriptor_set_layout.bindings;
                }
            }
            if (desc_bindings) {
                vk::DescriptorSetLayoutCreateInfo dslci{{}, *desc_bindings};
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
                    "Material %s pipeline %u has no material descriptors.",
                    pimpl->m_name.c_str(),
                    pass_index
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
                "Material template \"%s\" (pass %u) does not specify its color attachment format. "
                "Falling back to Swapchain default format. Gamma correction and color space may be incorrect.",
                this->pimpl->m_name.c_str(),
                pass_index
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
            // XXX: This case is not thoroughly tested!
            assert(
                prop.attachments.color.size() == prop.attachments.color_blending.size()
                && "Mismatched color attachment and blending operation size."
            );

            cbass.resize(prop.attachments.color_blending.size());

            for (size_t i = 0; i < prop.attachments.color.size(); i++) {
                if (prop.attachments.color[i] == ImageUtils::ImageFormat::UNDEFINED) {
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_RENDER,
                        "Material template \"%s\" (pass %u, color attachment %llu) does not specify its format. "
                        "Falling back to Swapchain default format. Gamma correction and color space may be incorrect.",
                        this->pimpl->m_name.c_str(),
                        pass_index,
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
            SDL_LOG_CATEGORY_RENDER, "Successfully created pass %u for material %s.", pass_index, pimpl->m_name.c_str()
        );

        pimpl->m_passes[pass_index] = std::move(pass_info);
    }

    void MaterialTemplate::CreatePipelines(const MaterialTemplateProperties &props) {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Createing pipelines for material %s.", pimpl->m_name.c_str());
        // Prepare descriptor pool
        vk::DescriptorPoolCreateInfo dpci{{}, PoolInfo::MAX_SET_SIZE, PoolInfo::DESCRIPTOR_POOL_SIZES, nullptr};
        vk::Device dvc = m_system.getDevice();
        pimpl->m_poolInfo.pool = dvc.createDescriptorPoolUnique(dpci);
        DEBUG_SET_NAME_TEMPLATE(
            dvc, pimpl->m_poolInfo.pool.get(), std::format("Desc Pool - Material {}", pimpl->m_name)
        );

        // Create a fallback point-clamp sampler
        vk::SamplerCreateInfo sci{};
        sci.magFilter = sci.minFilter = vk::Filter::eNearest;
        sci.addressModeU = sci.addressModeV = sci.addressModeW = vk::SamplerAddressMode::eRepeat;
        pimpl->m_default_sampler = dvc.createSamplerUnique(sci);

        // Create pipelines
        for (const auto &[key, val] : props.properties) {
            this->CreatePipeline(key, val, dvc);
        }
    }
    MaterialTemplate::MaterialTemplate(RenderSystem &system) : m_system(system), pimpl(std::make_unique<impl>()) {
    }

    void MaterialTemplate::Instantiate(const MaterialTemplateAsset &asset) {
        pimpl->m_name = asset.name;
        CreatePipelines(asset.properties);
    }

    MaterialTemplate::~MaterialTemplate() = default;

    std::shared_ptr<MaterialInstance> MaterialTemplate::CreateInstance() {
        return std::make_shared<MaterialInstance>(m_system, shared_from_this());
    }
    vk::Pipeline MaterialTemplate::GetPipeline(uint32_t pass_index) const noexcept {
        assert(pimpl->m_passes.contains(pass_index) && "Invaild pass index");
        return pimpl->m_passes.at(pass_index).pipeline.get();
    }
    vk::PipelineLayout MaterialTemplate::GetPipelineLayout(uint32_t pass_index) const noexcept {
        assert(pimpl->m_passes.contains(pass_index) && "Invaild pass index");
        return pimpl->m_passes.at(pass_index).pipeline_layout.get();
    }
    auto MaterialTemplate::GetAllPassInfo() const noexcept -> const decltype(pimpl->m_passes) & {
        return pimpl->m_passes;
    }
    auto MaterialTemplate::GetPassInfo(uint32_t pass_index) const -> const PassInfo & {
        return pimpl->m_passes.at(pass_index);
    }
    vk::DescriptorSetLayout MaterialTemplate::GetDescriptorSetLayout(uint32_t pass_index) const {
        assert(pimpl->m_passes.contains(pass_index) && "Invaild pass index");
        return pimpl->m_passes.at(pass_index).desc_layout.get();
    }
    vk::DescriptorSet MaterialTemplate::AllocateDescriptorSet(uint32_t pass_index) {
        assert(pimpl->m_passes.contains(pass_index) && "Invaild pass index");

        auto layout = pimpl->m_passes.at(pass_index).desc_layout.get();
        if (!layout) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER,
                "Allocating empty descriptor for material %s pass %u",
                pimpl->m_name.c_str(),
                pass_index
            );
            return nullptr;
        }

        vk::DescriptorSetAllocateInfo dsai{pimpl->m_poolInfo.pool.get(), {layout}};
        auto sets = m_system.getDevice().allocateDescriptorSets(dsai);
        return sets[0];
    }
    std::variant<
        std::monostate,
        std::reference_wrapper<const MaterialTemplate::DescVar>,
        std::reference_wrapper<const MaterialTemplate::InblockVar>>
    MaterialTemplate::GetVariable(const std::string &name, uint32_t pass_index) const {
        auto idx = this->GetVariableIndex(name, pass_index);
        if (!idx) return std::monostate{};
        if (idx.value().second) {
            return this->GetInBlockVariable(idx.value().first, pass_index);
        }
        return this->GetDescVariable(idx.value().first, pass_index);
    }
    const MaterialTemplate::DescVar &MaterialTemplate::GetDescVariable(uint32_t index, uint32_t pass_index) const {
        return pimpl->m_passes.at(pass_index).desc.vars.at(index);
    }
    const MaterialTemplate::InblockVar &MaterialTemplate::GetInBlockVariable(
        uint32_t index, uint32_t pass_index
    ) const {
        return pimpl->m_passes.at(pass_index).inblock.vars.at(index);
    }
    std::optional<std::pair<uint32_t, bool>> MaterialTemplate::GetVariableIndex(
        const std::string &name, uint32_t pass_index
    ) const noexcept {
        auto pitr = pimpl->m_passes.find(pass_index);
        assert(pitr != pimpl->m_passes.end() && "Invaild pass index");
        auto itr = pitr->second.inblock.names.find(name);
        if (itr == pitr->second.inblock.names.end()) {
            auto nitr = pitr->second.desc.names.find(name);
            return nitr == pitr->second.desc.names.end()
                       ? std::nullopt
                       : std::optional<std::pair<uint32_t, bool>>{std::make_pair(nitr->second, false)};
        }
        return std::make_pair(itr->second, true);
    }
    uint64_t MaterialTemplate::GetMaximalUBOSize(uint32_t pass_index) const {
        assert(pimpl->m_passes.contains(pass_index) && "Invaild pass index");
        return pimpl->m_passes.at(pass_index).inblock.maximal_ubo_size;
    }
    void MaterialTemplate::PlaceUBOVariables(
        const MaterialInstance &instance, std::vector<std::byte> &memory, uint32_t pass_index
    ) const {
        const auto &variables = instance.GetInBlockVariables(pass_index);
        const auto &pass_info = this->GetPassInfo(pass_index);

        PipelineInfo::PlaceUBOVariables(variables, pass_info, memory);
    }
    std::vector<std::pair<uint32_t, vk::DescriptorImageInfo>> MaterialTemplate::GetDescriptorImageInfo(
        const MaterialInstance &instance, uint32_t pass_index
    ) const {
        const auto &pass_info = this->GetPassInfo(pass_index);
        const auto &instance_vars = instance.GetDescVariables(pass_index);
        return PipelineInfo::GetDescriptorImageInfo(instance_vars, pass_info, pimpl->m_default_sampler.get());
    }
} // namespace Engine
