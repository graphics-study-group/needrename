#ifndef RENDER_MATERIAL_MATERIALTEMPLATEUTILS_INCLUDED
#define RENDER_MATERIAL_MATERIALTEMPLATEUTILS_INCLUDED

#include <vulkan/vulkan.hpp>
#include <Asset/Material/MaterialTemplateAsset.h>
#include <Asset/Material/ShaderAsset.h>
#include <unordered_set>
#include <typeindex>
#include <glm.hpp>

namespace Engine{
    namespace PipelineUtils{
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

        /**
         * Converts a ShaderAsset::ShaderType to a Vulkan shader stage flag bits.
         *
         * @param type The shader type to convert.
         * @return The corresponding Vulkan shader stage flag bits.
         */ 
        vk::ShaderStageFlagBits
        ToVulkanShaderStageFlagBits(ShaderAsset::ShaderType type);

        /**
         * Converts a MaterialTemplateSinglePassProperties::RasterizerProperties to a Vulkan pipeline rasterization state create info.
         *
         * @param props The rasterizer properties to convert.
         * @return The corresponding Vulkan pipeline rasterization state create info.
         */
        vk::PipelineRasterizationStateCreateInfo
        ToVulkanRasterizationStateCreateInfo(const MaterialTemplateSinglePassProperties::RasterizerProperties &props);

        /**
         * Converts a MaterialTemplateSinglePassProperties::DSProperties to a Vulkan pipeline depth stencil state create info.
         *
         * @param props The DS properties to convert.
         * @return The corresponding Vulkan pipeline depth stencil state create info.
         */
        vk::PipelineDepthStencilStateCreateInfo
        ToVulkanDepthStencilStateCreateInfo(const MaterialTemplateSinglePassProperties::DSProperties &props);

        /**
         * Converts a MaterialTemplateSinglePassProperties::Shaders and a default sampler to a list of Vulkan descriptor set layout bindings.
         *
         * @param shaders The shaders to convert.
         * @return A list of Vulkan descriptor set layout bindings.
         */
        std::vector<vk::DescriptorSetLayoutBinding>
        ToVulkanDescriptorSetLayoutBindings(const MaterialTemplateSinglePassProperties::Shaders &shaders);

        /**
         * Converts a MaterialTemplateSinglePassProperties::Shaders and a vector of unique shader modules to a Vulkan pipeline shader stage create info.
         *
         * @param shaders The shaders to convert.
         * @param shader_modules The unique shader modules to use for the stages.
         * @return The corresponding Vulkan pipeline shader stage create info.
         */
        vk::PipelineShaderStageCreateInfo
        ToVulkanShaderStageCreateInfo(const MaterialTemplateSinglePassProperties::Shaders &shaders, std::vector<vk::UniqueShaderModule> &shader_modules);

        /**
         * Converts a MaterialTemplateSinglePassProperties::Shaders to a Vulkan pipeline layout create info.
         *
         * @param shaders The shaders to convert.
         * @return The corresponding Vulkan pipeline layout create info.
         */
        vk::PipelineLayoutCreateInfo
        ToVulkanPipelineLayoutCreateInfo(const MaterialTemplateSinglePassProperties::Shaders &shaders);

        /**
         * Converts a MaterialTemplateSinglePassProperties::Attachments to a Vulkan pipeline rendering create info.
         *
         * @param attachments The attachments to convert.
         * @return The corresponding Vulkan pipeline rendering create info.
         */
        vk::PipelineRenderingCreateInfo
        ToVulkanPipelineRenderingCreateInfo(const MaterialTemplateSinglePassProperties::Attachments &attachments);
    }
}

#endif // RENDER_MATERIAL_MATERIALTEMPLATEUTILS_INCLUDED
