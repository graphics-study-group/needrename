#include "MaterialTemplateUtils.h"
#include <ranges>
#include <SDL3/SDL.h>

namespace Engine {
    namespace MaterialTemplateUtils {
        vk::PolygonMode ToVkPolygonMode(MaterialTemplateSinglePassProperties::RasterizerProperties::FillingMode mode)
        {
            
            switch (mode) {
            case FillingMode::Fill:
                return vk::PolygonMode::eFill;
            case FillingMode::Line:
                return vk::PolygonMode::eLine;
            case FillingMode::Point:
                return vk::PolygonMode::ePoint;
            }
            __builtin_unreachable();
        }
        vk::CullModeFlags ToVkCullMode(MaterialTemplateSinglePassProperties::RasterizerProperties::CullingMode mode)
        {
            
            switch (mode) {
            case CullingMode::None:
                return vk::CullModeFlagBits::eNone;
            case CullingMode::Front:
                return vk::CullModeFlagBits::eFront;
            case CullingMode::Back:
                return vk::CullModeFlagBits::eBack;
            case CullingMode::All:
                return vk::CullModeFlagBits::eFrontAndBack;
            }
            __builtin_unreachable();
        }
        vk::FrontFace ToVkFrontFace(MaterialTemplateSinglePassProperties::RasterizerProperties::FrontFace face)
        {
            switch (face){
            case FrontFace::Counterclockwise:
                return vk::FrontFace::eCounterClockwise;
            case FrontFace::Clockwise:
                return vk::FrontFace::eClockwise;
            }
            __builtin_unreachable();
        }

        vk::ShaderStageFlagBits ToVulkanShaderStageFlagBits(ShaderAsset::ShaderType type)
        {
            using Type = ShaderAsset::ShaderType;
            switch(type) {
            case Type::Fragment:
                return vk::ShaderStageFlagBits::eFragment;
            case Type::Vertex:
                return vk::ShaderStageFlagBits::eVertex;
            default:
                return vk::ShaderStageFlagBits::eAll;
            }
        }

        vk::PipelineRasterizationStateCreateInfo ToVulkanRasterizationStateCreateInfo(const MaterialTemplateSinglePassProperties::RasterizerProperties & prop)
        {
            vk::PipelineRasterizationStateCreateInfo info{};
            info.depthClampEnable = vk::False;
            info.rasterizerDiscardEnable = vk::False;
            info.polygonMode = ToVkPolygonMode(prop.filling);
            info.lineWidth = prop.line_width;
            info.cullMode = ToVkCullMode(prop.culling);
            info.frontFace = ToVkFrontFace(prop.front);
            info.depthBiasEnable = vk::False;
            return info;
        }

        vk::PipelineDepthStencilStateCreateInfo
        ToVulkanDepthStencilStateCreateInfo(const MaterialTemplateSinglePassProperties::DSProperties & p) {
            vk::PipelineDepthStencilStateCreateInfo info{
                vk::PipelineDepthStencilStateCreateFlags{}, 
                p.ds_test_enabled, p.ds_write_enabled,
                vk::CompareOp::eLess,   // Lower => closer
                vk::False,
                vk::False,
                {}, {},
                p.min_depth, p.max_depth
            };
            return info;
        }

        std::vector<vk::DescriptorSetLayoutBinding>
        ToVulkanDescriptorSetLayoutBindings(const MaterialTemplateSinglePassProperties::Shaders & p, vk::Sampler default_sampler){
#ifndef NDEBUG
            // Test whether scene and camera uniforms are compatible
            auto scene_uniforms_range = std::ranges::take_while_view(
                p.uniforms, 
                [](const ShaderVariableProperty & prop) -> bool {
                    return prop.frequency == ShaderVariableProperty::Frequency::PerScene;
                }
            );
            if (!scene_uniforms_range.empty()) SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Found non-compatible scene uniforms.");

            auto camera_uniforms_range = std::ranges::take_while_view(
                p.uniforms, 
                [](const ShaderVariableProperty & prop) -> bool {
                    return prop.frequency == ShaderVariableProperty::Frequency::PerCamera;
                }
            );
            for (const auto & prop : camera_uniforms_range) {
                using Type = ShaderVariableProperty::Type;
                if (prop.type != Type::Mat4 || (prop.offset != 0 && prop.offset != 64) || (prop.binding != 0)) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Found non-compatible camera uniforms %s.", prop.name.c_str());
                }
            }
#endif

            // Filter out non-material descriptors. Descriptors for scenes or cameras are managed seperately.
            auto material_uniforms_range = std::ranges::take_while_view(
                p.uniforms, 
                [](const ShaderVariableProperty & prop) -> bool {
                    return prop.frequency == ShaderVariableProperty::Frequency::PerMaterial;
                }
            );

            std::vector <vk::DescriptorSetLayoutBinding> bindings;
            // Prepare default UBO
            bindings.push_back(vk::DescriptorSetLayoutBinding{
                0,
                vk::DescriptorType::eUniformBuffer,
                1,
                vk::ShaderStageFlagBits::eAll,
                {}
            });
            for (const ShaderVariableProperty & prop : material_uniforms_range) {

                if (prop.binding == 0) {
                    if (!ShaderVariableProperty::InUBO(prop.type)) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Non-UBO descriptor occupying UBO binding. This descriptor is ignored.");
                    }
                    continue;
                }

                if (ShaderVariableProperty::InUBO(prop.type)) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "UBO descriptor not in binding zero. This descriptor is ignored.");
                    continue;
                }
                if (prop.offset != 0) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Non-zero offset for uniforms outside UBO. Offset is ignored.");
                }
                bindings.push_back(vk::DescriptorSetLayoutBinding{
                    prop.binding,
                    vk::DescriptorType::eCombinedImageSampler,
                    1,
                    vk::ShaderStageFlagBits::eAll,
                    {}
                });
            }
            return bindings;
        }

        vk::PipelineShaderStageCreateInfo
        ToVulkanShaderStageCreateInfo(const MaterialTemplateSinglePassProperties::Shaders & p, std::vector<vk::UniqueShaderModule> & v) {
            v.clear();
        }

        vk::PipelineLayoutCreateInfo
        ToVulkanPipelineLayoutCreateInfo(const MaterialTemplateSinglePassProperties::Shaders & p) {

        }

        vk::PipelineRenderingCreateInfo
        ToVulkanPipelineRenderingCreateInfo(const MaterialTemplateSinglePassProperties::Attachments & p){

        }
    }
};
