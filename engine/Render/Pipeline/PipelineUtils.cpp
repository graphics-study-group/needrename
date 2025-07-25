#include "PipelineUtils.h"

#include "Asset/Material/PipelineProperty.h"

#include <SDL3/SDL.h>
#include <ranges>

namespace Engine {
    namespace PipelineUtils {
        vk::PolygonMode ToVkPolygonMode(FillingMode mode) {

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
        vk::CullModeFlags ToVkCullMode(CullingMode mode) {

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
        vk::FrontFace ToVkFrontFace(FrontFace face) {
            switch (face) {
            case FrontFace::Counterclockwise:
                return vk::FrontFace::eCounterClockwise;
            case FrontFace::Clockwise:
                return vk::FrontFace::eClockwise;
            }
            __builtin_unreachable();
        }

        vk::CompareOp ToVkCompareOp(DSComparator comp) {
            return static_cast<vk::CompareOp>(static_cast<int>(comp));
        }

        vk::StencilOp ToVkStencilOp(StencilOperation op) {
            return static_cast<vk::StencilOp>(static_cast<int>(op));
        }

        vk::BlendOp ToVkBlendOp(BlendOperation op) {
            switch (op) {
            case BlendOperation::None:
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Requesting blend operation \"None\"");
                return vk::BlendOp::eZeroEXT;
            case BlendOperation::Add:
                return vk::BlendOp::eAdd;
            case BlendOperation::Substract:
                return vk::BlendOp::eSubtract;
            case BlendOperation::ReverseSubstract:
                return vk::BlendOp::eReverseSubtract;
            case BlendOperation::Min:
                return vk::BlendOp::eMin;
            case BlendOperation::Max:
                return vk::BlendOp::eMax;
            }
            __builtin_unreachable();
        }

        vk::BlendFactor ToVkBlendFactor(BlendFactor factor) {
            switch (factor) {
            case BlendFactor::Zero:
                return vk::BlendFactor::eZero;
            case BlendFactor::One:
                return vk::BlendFactor::eOne;
            case BlendFactor::SrcColor:
                return vk::BlendFactor::eSrcColor;
            case BlendFactor::OneMinusSrcColor:
                return vk::BlendFactor::eOneMinusSrcColor;
            case BlendFactor::DstColor:
                return vk::BlendFactor::eDstColor;
            case BlendFactor::OneMinusDstColor:
                return vk::BlendFactor::eOneMinusDstColor;
            case BlendFactor::SrcAlpha:
                return vk::BlendFactor::eSrcAlpha;
            case BlendFactor::OneMinusSrcAlpha:
                return vk::BlendFactor::eOneMinusSrcAlpha;
            case BlendFactor::DstAlpha:
                return vk::BlendFactor::eDstAlpha;
            case BlendFactor::OneMinusDstAlpha:
                return vk::BlendFactor::eOneMinusDstAlpha;
            }
            __builtin_unreachable();
        }

        vk::ShaderStageFlagBits ToVulkanShaderStageFlagBits(ShaderAsset::ShaderType type) {
            using Type = ShaderAsset::ShaderType;
            switch (type) {
            case Type::Fragment:
                return vk::ShaderStageFlagBits::eFragment;
            case Type::Vertex:
                return vk::ShaderStageFlagBits::eVertex;
            case Type::Compute:
                return vk::ShaderStageFlagBits::eCompute;
            default:
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Unsupported shader stage.");
                return vk::ShaderStageFlagBits::eAll;
            }
        }

        vk::PipelineRasterizationStateCreateInfo ToVulkanRasterizationStateCreateInfo(
            const PipelineProperties::RasterizerProperties &prop) {
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

        vk::PipelineDepthStencilStateCreateInfo ToVulkanDepthStencilStateCreateInfo(
            const PipelineProperties::DSProperties &p) {
            vk::PipelineDepthStencilStateCreateInfo info{vk::PipelineDepthStencilStateCreateFlags{},
                                                         p.depth_test_enable,
                                                         p.depth_write_enable,
                                                         ToVkCompareOp(p.depth_comparator), // Lower => closer
                                                         p.depth_bound_test_enable,
                                                         p.stencil_test_enable,
                                                         {ToVkStencilOp(p.stencil_front.fail_op),
                                                          ToVkStencilOp(p.stencil_front.pass_op),
                                                          ToVkStencilOp(p.stencil_front.zfail_op),
                                                          ToVkCompareOp(p.stencil_front.comparator),
                                                          p.stencil_front.compare_mask,
                                                          p.stencil_front.write_mask,
                                                          p.stencil_front.reference},
                                                         {ToVkStencilOp(p.stencil_back.fail_op),
                                                          ToVkStencilOp(p.stencil_back.pass_op),
                                                          ToVkStencilOp(p.stencil_back.zfail_op),
                                                          ToVkCompareOp(p.stencil_back.comparator),
                                                          p.stencil_back.compare_mask,
                                                          p.stencil_back.write_mask,
                                                          p.stencil_back.reference},
                                                         p.min_depth,
                                                         p.max_depth};
            return info;
        }

        std::vector<vk::DescriptorSetLayoutBinding> ToVulkanDescriptorSetLayoutBindings(
            const PipelineProperties::Shaders &p) {
#ifndef NDEBUG
            // Test whether scene uniforms are compatible
            auto scene_uniforms_range =
                std::ranges::filter_view(p.uniforms, [](const ShaderVariableProperty &prop) -> bool {
                    return prop.frequency == ShaderVariableProperty::Frequency::PerScene;
                });
            if (!scene_uniforms_range.empty())
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                            "Found non-compatible scene uniforms. Scene uniform should be empty.");

            // Test wheter camera uniforms are compatible
            size_t found_camera_uniform = false;
            for (size_t i = 0; i < p.uniforms.size(); i++) {
                if (p.uniforms[i].frequency == ShaderVariableProperty::Frequency::PerCamera) {
                    if (found_camera_uniform) {
                        SDL_LogWarn(
                            SDL_LOG_CATEGORY_RENDER,
                            "Found too many camera uniforms. Camera uniform should contain exactly one UBO only.");
                    }
                    found_camera_uniform = true;
                    if (p.uniforms[i].type != ShaderVariableProperty::Type::UBO) {
                        SDL_LogWarn(
                            SDL_LOG_CATEGORY_RENDER,
                            "Found camera uniform of wrong type. Camera uniform should contain exactly one UBO only.");
                    }
                    if (p.uniforms[i].binding != 0) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                                    "Found camera uniform of wrong binding. Camera uniform should "
                                    "contain exactly one UBO bound to the zeroth binding.");
                    }
                }
            }

            auto camera_uniforms_variable_range =
                std::ranges::filter_view(p.ubo_variables, [](const ShaderInBlockVariableProperty &prop) -> bool {
                    return prop.frequency == ShaderVariableProperty::Frequency::PerCamera;
                });
            for (const auto &prop : camera_uniforms_variable_range) {
                using Type = ShaderInBlockVariableProperty::InBlockVarType;
                if (prop.type != Type::Mat4 || (prop.offset != 0 && prop.offset != 64) || (prop.binding != 0)) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Found non-compatible camera uniforms %s.", prop.name.c_str());
                }
            }

            // Test wheter material uniform variables binds to UBO
            {
                uint32_t material_ubo_binding = std::numeric_limits<uint32_t>::max();
                auto material_uniforms_range =
                    std::ranges::filter_view(p.uniforms, [](const ShaderVariableProperty &prop) -> bool {
                        return prop.frequency == ShaderVariableProperty::Frequency::PerMaterial;
                    });
                for (const auto &u : material_uniforms_range) {
                    if (u.type == ShaderVariableProperty::Type::UBO) {
                        if (material_ubo_binding != std::numeric_limits<uint32_t>::max()) {
                            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Found multiple material UBOs %s.", u.name.c_str());
                        }
                        material_ubo_binding = u.binding;
                        if (material_ubo_binding != 0) {
                            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                                        "Material UBOs %s not bound to the zeroth binding.",
                                        u.name.c_str());
                        }
                    }
                }
                if (material_ubo_binding == std::numeric_limits<uint32_t>::max()) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Material does not have any UBOs.");
                }
                auto material_uniforms_variable_range =
                    std::ranges::filter_view(p.ubo_variables, [](const ShaderInBlockVariableProperty &prop) -> bool {
                        return prop.frequency == ShaderVariableProperty::Frequency::PerMaterial;
                    });
                for (const auto &uv : material_uniforms_variable_range) {
                    if (uv.binding != material_ubo_binding) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                                    "Material UBO variable %s not bound to the correct binding.",
                                    uv.name.c_str());
                    }
                }
            }
#endif

            // Filter out non-material descriptors. Descriptors for scenes or cameras are managed seperately.
            auto material_uniforms_range =
                std::ranges::filter_view(p.uniforms, [](const ShaderVariableProperty &prop) -> bool {
                    return prop.frequency == ShaderVariableProperty::Frequency::PerMaterial;
                });

            std::vector<vk::DescriptorSetLayoutBinding> bindings;
            for (const ShaderVariableProperty &prop : material_uniforms_range) {

                switch (prop.type) {
                    using Type = ShaderVariableProperty::Type;
                case Type::UBO:
                    bindings.push_back(vk::DescriptorSetLayoutBinding{
                        prop.binding, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eAll, 0});
                    break;
                case Type::StorageBuffer:
                    bindings.push_back(vk::DescriptorSetLayoutBinding{
                        prop.binding, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAll, 0});
                    break;
                case Type::Texture:
                    bindings.push_back(vk::DescriptorSetLayoutBinding{
                        prop.binding, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eAll, {}});
                    break;
                case Type::StorageImage:
                    bindings.push_back(vk::DescriptorSetLayoutBinding{
                        prop.binding, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eAll, {}});
                    break;
                default:
                    SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Unsupported out-of-UBO property type.");
                }
            }
            return bindings;
        }

        vk::PipelineShaderStageCreateInfo ToVulkanShaderStageCreateInfo(const PipelineProperties::Shaders &p,
                                                                        std::vector<vk::UniqueShaderModule> &v) {
            v.clear();
        }

        vk::PipelineLayoutCreateInfo ToVulkanPipelineLayoutCreateInfo(const PipelineProperties::Shaders &p) {
        }

        vk::PipelineRenderingCreateInfo ToVulkanPipelineRenderingCreateInfo(const PipelineProperties::Attachments &p) {
        }
    } // namespace PipelineUtils
}; // namespace Engine
