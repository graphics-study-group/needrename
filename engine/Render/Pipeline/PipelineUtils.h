#ifndef RENDER_PIPELINE_PIPELINEUTILS_INCLUDED
#define RENDER_PIPELINE_PIPELINEUTILS_INCLUDED

#include "Render/Pipeline/PipelineEnums.h"
#include "Asset/Material/PipelineProperty.h"
#include <unordered_set>
#include <typeindex>
#include <glm.hpp>

namespace vk {
    enum class PolygonMode;
    // enum class CullModeFlags;
    enum class FrontFace;
    enum class CompareOp;
    enum class StencilOp;
    enum class BlendOp;
    enum class BlendFactor;
    class PipelineRasterizationStateCreateInfo;
    class PipelineDepthStencilStateCreateInfo;
    class DescriptorSetLayoutBinding;
    class PipelineShaderStageCreateInfo;
    class PipelineLayoutCreateInfo;
    class PipelineRenderingCreateInfo;
}


namespace Engine{

    namespace PipelineProperties {
        struct RasterizerProperties;
        struct DSProperties;
        struct Shaders;
        struct Attachments;
    };

    namespace PipelineUtils{
        const static std::unordered_set <std::type_index> REGISTERED_SHADER_UNIFORM_TYPES = {
            typeid(int),
            typeid(float),
            typeid(glm::vec4),
            typeid(glm::mat4)
        };

        vk::PolygonMode ToVkPolygonMode(FillingMode mode);
        vk::FrontFace ToVkFrontFace(FrontFace face);
        vk::CompareOp ToVkCompareOp(DSComparator comp);
        vk::StencilOp ToVkStencilOp(StencilOperation op);
        vk::BlendOp ToVkBlendOp(BlendOperation op);
        vk::BlendFactor ToVkBlendFactor(BlendFactor factor);

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
        ToVulkanRasterizationStateCreateInfo(const PipelineProperties::RasterizerProperties &props);

        /**
         * Converts a MaterialTemplateSinglePassProperties::DSProperties to a Vulkan pipeline depth stencil state create info.
         *
         * @param props The DS properties to convert.
         * @return The corresponding Vulkan pipeline depth stencil state create info.
         */
        vk::PipelineDepthStencilStateCreateInfo
        ToVulkanDepthStencilStateCreateInfo(const PipelineProperties::DSProperties &props);

        /**
         * Converts a MaterialTemplateSinglePassProperties::Shaders and a default sampler to a list of Vulkan descriptor set layout bindings.
         *
         * @param shaders The shaders to convert.
         * @return A list of Vulkan descriptor set layout bindings.
         */
        [[deprecated]]
        std::vector<vk::DescriptorSetLayoutBinding>
        ToVulkanDescriptorSetLayoutBindings(const PipelineProperties::Shaders &shaders);

        /**
         * Converts a MaterialTemplateSinglePassProperties::Shaders and a vector of unique shader modules to a Vulkan pipeline shader stage create info.
         *
         * @param shaders The shaders to convert.
         * @param shader_modules The unique shader modules to use for the stages.
         * @return The corresponding Vulkan pipeline shader stage create info.
         */
        vk::PipelineShaderStageCreateInfo
        ToVulkanShaderStageCreateInfo(const PipelineProperties::Shaders &shaders, std::vector<vk::UniqueShaderModule> &shader_modules);

        /**
         * Converts a MaterialTemplateSinglePassProperties::Shaders to a Vulkan pipeline layout create info.
         *
         * @param shaders The shaders to convert.
         * @return The corresponding Vulkan pipeline layout create info.
         */
        vk::PipelineLayoutCreateInfo
        ToVulkanPipelineLayoutCreateInfo(const PipelineProperties::Shaders &shaders);

        /**
         * Converts a MaterialTemplateSinglePassProperties::Attachments to a Vulkan pipeline rendering create info.
         *
         * @param attachments The attachments to convert.
         * @return The corresponding Vulkan pipeline rendering create info.
         */
        vk::PipelineRenderingCreateInfo
        ToVulkanPipelineRenderingCreateInfo(const PipelineProperties::Attachments &attachments);
    }
}

#endif // RENDER_PIPELINE_PIPELINEUTILS_INCLUDED
