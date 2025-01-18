#include "MaterialTemplate.h"
#include "Asset/AssetRef.h"
#include "Render/RenderSystem.h"
#include "MaterialTemplateUtils.h"
#include "Render/ConstantData/PerModelConstants.h"
#include "Render/Renderer/HomogeneousMesh.h"
#include <fstream>

inline std::vector <char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) throw std::runtime_error("failed to open file!");

    size_t fileSize = (size_t) file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

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

            // TODO: supply a default sampler.
            auto desc_bindings = MaterialTemplateUtils::ToVulkanDescriptorSetLayoutBindings(prop.shaders, m_default_sampler.get());
            vk::DescriptorSetLayoutCreateInfo dslci {
                {}, desc_bindings
            };
            pass_info.desc_layout = device.createDescriptorSetLayoutUnique(dslci);

            vk::PipelineLayoutCreateInfo plci;
            std::array <vk::PushConstantRange, 1> push_constants{ConstantData::PerModelConstantPushConstant::GetPushConstantRange()};
            std::vector <vk::DescriptorSetLayout> set_layouts{pool.GetPerSceneConstantLayout().get(), pool.GetPerCameraConstantLayout().get()};
            set_layouts.push_back(pass_info.desc_layout.get());
            pass_info.pipeline_layout = device.createPipelineLayoutUnique(plci);
        }

        // Create shaders
        std::vector <vk::PipelineShaderStageCreateInfo> psscis;
        pass_info.shaders.resize(prop.shaders.shaders.size());
        psscis.resize(prop.shaders.shaders.size());
        for (size_t i = 0; i < prop.shaders.shaders.size(); i++) {
            auto shader_asset = prop.shaders.shaders[i].cas<ShaderAsset>();
            auto code = readFile(shader_asset->filename);
            vk::ShaderModuleCreateInfo ci {
                {},
                code.size(),
                reinterpret_cast<const uint32_t *> (code.data())
            };
            pass_info.shaders[i] = device.createShaderModuleUnique(ci);
            psscis[i] = vk::PipelineShaderStageCreateInfo {
                {},
                MaterialTemplateUtils::ToVulkanShaderStageFlagBits(shader_asset->shaderType),
                pass_info.shaders[i].get(),
                "main"
            };
        }

        bool use_swapchain_attachments = prop.attachments.color.empty();

        auto vis = HomogeneousMesh::GetVertexInputState();
        auto iasi = vk::PipelineInputAssemblyStateCreateInfo {{}, vk::PrimitiveTopology::eTriangleList, vk::False};
        auto vsi = vk::PipelineViewportStateCreateInfo {{}, 1, nullptr, 1, nullptr};
        auto rsci = MaterialTemplateUtils::ToVulkanRasterizationStateCreateInfo(prop.rasterizer);
        auto msi = vk::PipelineMultisampleStateCreateInfo{{}, vk::SampleCountFlagBits::e1, vk::False};
        auto dsci = MaterialTemplateUtils::ToVulkanDepthStencilStateCreateInfo(prop.depth_stencil);
        auto dsi = vk::PipelineDynamicStateCreateInfo{{}, PassInfo::PIPELINE_DYNAMIC_STATES};
        
        vk::PipelineColorBlendStateCreateInfo cbsi {};
        vk::PipelineRenderingCreateInfo prci {};
        std::vector<vk::PipelineColorBlendAttachmentState> cbass;
        // Fill in attachment information
        if (use_swapchain_attachments) {
            prci = m_system.lock()->GetSwapchain().GetPipelineRenderingCreateInfo();
            cbass.push_back({vk::False});
            pass_info.attachments.color_attachment_ops.push_back({
                vk::AttachmentLoadOp::eClear, 
                vk::AttachmentStoreOp::eStore,
                vk::ClearValue{vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f}}
            });
            pass_info.attachments.ds_attachment_ops = {
                vk::AttachmentLoadOp::eClear,
                vk::AttachmentStoreOp::eDontCare,
                vk::ClearValue{vk::ClearDepthStencilValue{1.0f, 0u}}
            };
        } else {
            SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Custom attachments not implemented yet.");
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
    }
} // namespace Engine
