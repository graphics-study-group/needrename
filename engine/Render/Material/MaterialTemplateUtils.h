#ifndef RENDER_MATERIAL_MATERIALTEMPLATEUTILS_INCLUDED
#define RENDER_MATERIAL_MATERIALTEMPLATEUTILS_INCLUDED

#include <vulkan/vulkan.hpp>
#include <Asset/Material/MaterialTemplateAsset.h>
#include <Asset/Material/ShaderAsset.h>
#include <unordered_set>
#include <typeindex>

namespace Engine{
    namespace MaterialTemplateUtils{
        const static std::unordered_set <std::type_index> REGISTERED_SHADER_UNIFORM_TYPES = {
            typeid(int),
            typeid(float),
            typeid(glm::vec4),
            typeid(glm::mat4)
        };

        using FillingMode = MaterialTemplateSinglePassProperties::RasterizerProperties::FillingMode;
        using CullingMode = MaterialTemplateSinglePassProperties::RasterizerProperties::CullingMode;
        using FrontFace = MaterialTemplateSinglePassProperties::RasterizerProperties::FrontFace;

        vk::PolygonMode ToVkPolygonMode(FillingMode mode);
        vk::CullModeFlags ToVkCullMode(CullingMode mode);
        vk::FrontFace ToVkFrontFace(FrontFace face);

        vk::ShaderStageFlagBits
        ToVulkanShaderStageFlagBits(ShaderAsset::ShaderType type);

        vk::PipelineRasterizationStateCreateInfo
        ToVulkanRasterizationStateCreateInfo(const MaterialTemplateSinglePassProperties::RasterizerProperties &);
        vk::PipelineDepthStencilStateCreateInfo
        ToVulkanDepthStencilStateCreateInfo(const MaterialTemplateSinglePassProperties::DSProperties &);

        std::vector<vk::DescriptorSetLayoutBinding>
        ToVulkanDescriptorSetLayoutBindings(const MaterialTemplateSinglePassProperties::Shaders &, vk::Sampler default_sampler);
        vk::PipelineShaderStageCreateInfo
        ToVulkanShaderStageCreateInfo(const MaterialTemplateSinglePassProperties::Shaders &, std::vector<vk::UniqueShaderModule> &);
        vk::PipelineLayoutCreateInfo
        ToVulkanPipelineLayoutCreateInfo(const MaterialTemplateSinglePassProperties::Shaders &);

        vk::PipelineRenderingCreateInfo
        ToVulkanPipelineRenderingCreateInfo(const MaterialTemplateSinglePassProperties::Attachments &);
    }
}

#endif // RENDER_MATERIAL_MATERIALTEMPLATEUTILS_INCLUDED
