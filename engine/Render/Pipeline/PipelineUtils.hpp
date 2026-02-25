#ifndef RENDER_PIPELINE_PIPELINEUTILS
#define RENDER_PIPELINE_PIPELINEUTILS

#include <vulkan/vulkan.hpp>

#include "Asset/Material/PipelineProperty.h"
#include "Render/Pipeline/PipelineEnums.h"

namespace Engine::PipelineUtils {
    constexpr vk::PolygonMode ToVkPolygonMode(FillingMode mode) {
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
    constexpr vk::CullModeFlags ToVkCullMode(CullingMode mode) {
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
    constexpr vk::FrontFace ToVkFrontFace(FrontFace face) {
        switch (face) {
        case FrontFace::Counterclockwise:
            return vk::FrontFace::eCounterClockwise;
        case FrontFace::Clockwise:
            return vk::FrontFace::eClockwise;
        }
        __builtin_unreachable();
    }
    constexpr vk::CompareOp ToVkCompareOp(DSComparator comp) {
        return static_cast<vk::CompareOp>(static_cast<int>(comp));
    }
    constexpr vk::StencilOp ToVkStencilOp(StencilOperation op) {
        return static_cast<vk::StencilOp>(static_cast<int>(op));
    }
    constexpr vk::BlendOp ToVkBlendOp(BlendOperation op) {
        switch (op) {
        case BlendOperation::None:
            throw std::invalid_argument("Requesting special blending operation \"None\"");
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

    constexpr vk::BlendFactor ToVkBlendFactor(BlendFactor factor) {
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

    /**
     * Converts a ShaderAsset::ShaderType to a Vulkan shader stage flag bits.
     *
     *
     * @param type The shader type to convert.
     * @return The corresponding Vulkan shader stage flag bits.
     */
    constexpr vk::ShaderStageFlagBits ToVulkanShaderStageFlagBits(
        ShaderAsset::ShaderType type
    ) {
        using Type = ShaderAsset::ShaderType;
        switch (type) {
        case Type::Fragment:
            return vk::ShaderStageFlagBits::eFragment;
        case Type::Vertex:
            return vk::ShaderStageFlagBits::eVertex;
        case Type::Compute:
            return vk::ShaderStageFlagBits::eCompute;
        default:
            throw std::invalid_argument("Unsupported shader stage.");
            return vk::ShaderStageFlagBits::eAll;
        }
    }

    /**
     * Converts a MaterialTemplateSinglePassProperties::RasterizerProperties to a Vulkan pipeline
     * rasterization state create info.
     *
     * @param props The rasterizer properties to convert.
     * @return The corresponding Vulkan pipeline rasterization state create info.
     */
    constexpr vk::PipelineRasterizationStateCreateInfo ToVulkanRasterizationStateCreateInfo(
        const PipelineProperties::RasterizerProperties &prop
    ) {
        vk::PipelineRasterizationStateCreateInfo info{};
        info.depthClampEnable = vk::False;
        info.rasterizerDiscardEnable = vk::False;
        info.polygonMode = ToVkPolygonMode(prop.filling);
        info.lineWidth = prop.line_width;
        info.cullMode = ToVkCullMode(prop.culling);
        info.frontFace = ToVkFrontFace(prop.front);

        bool is_depth_bias_enabled = std::isfinite(prop.depth_bias_constant) && std::isfinite(prop.depth_bias_slope);
        is_depth_bias_enabled = is_depth_bias_enabled && (prop.depth_bias_constant != 0.0f && prop.depth_bias_constant != -0.0f);
        is_depth_bias_enabled = is_depth_bias_enabled && (prop.depth_bias_slope != 0.0f && prop.depth_bias_slope != -0.0f);
        if (is_depth_bias_enabled) {
            info.depthBiasEnable = vk::True;
            info.depthBiasConstantFactor = prop.depth_bias_constant;
            info.depthBiasSlopeFactor = prop.depth_bias_slope;
        } else {
            info.depthBiasEnable = vk::False;
        }

        return info;
    }

    /**
     * Converts a MaterialTemplateSinglePassProperties::DSProperties to a Vulkan pipeline depth stencil
     * state create info.
     *
     * @param props The DS properties to convert.
     * @return The
     * corresponding Vulkan pipeline depth stencil state create info.
     */
    constexpr vk::PipelineDepthStencilStateCreateInfo ToVulkanDepthStencilStateCreateInfo(
        const PipelineProperties::DSProperties &p
    ) {
        vk::PipelineDepthStencilStateCreateInfo info{
            vk::PipelineDepthStencilStateCreateFlags{},
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
            p.max_depth
        };
        return info;
    }
}

#endif // RENDER_PIPELINE_PIPELINEUTILS
