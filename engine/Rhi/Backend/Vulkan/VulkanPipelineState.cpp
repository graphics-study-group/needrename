#include "Rhi/Backend/Vulkan/VulkanPipelineState.h"

#include "Rhi/Backend/Vulkan/VulkanImageUtils.h"
#include "Rhi/Backend/Vulkan/VulkanPipelineEnums.h"

namespace Engine::Rhi {
    namespace {
        vk::Format ToVkVertexFormat(VertexFormat format) {
            return GetVkVertexFormat(format);
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
            default:
                return vk::BlendFactor::eOneMinusDstAlpha;
            }
        }

        vk::BlendOp ToVkBlendOperation(BlendOperation operation) {
            switch (operation) {
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
            case BlendOperation::None:
            default:
                return vk::BlendOp::eAdd;
            }
        }

        vk::PolygonMode ToVkPolygonMode(FillingMode mode) {
            return mode == FillingMode::Line ? vk::PolygonMode::eLine : vk::PolygonMode::eFill;
        }

        vk::CullModeFlags ToVkCullMode(CullingMode mode) {
            switch (mode) {
            case CullingMode::Front:
                return vk::CullModeFlagBits::eFront;
            case CullingMode::Back:
                return vk::CullModeFlagBits::eBack;
            case CullingMode::None:
            default:
                return vk::CullModeFlagBits::eNone;
            }
        }

        vk::FrontFace ToVkFrontFace(FrontFace face) {
            return face == FrontFace::Clockwise ? vk::FrontFace::eClockwise : vk::FrontFace::eCounterClockwise;
        }

        vk::CompareOp ToVkCompareOperation(DSComparator comparator) {
            switch (comparator) {
            case DSComparator::Never:
                return vk::CompareOp::eNever;
            case DSComparator::Less:
                return vk::CompareOp::eLess;
            case DSComparator::Equal:
                return vk::CompareOp::eEqual;
            case DSComparator::LEqual:
                return vk::CompareOp::eLessOrEqual;
            case DSComparator::Greater:
                return vk::CompareOp::eGreater;
            case DSComparator::NEqual:
                return vk::CompareOp::eNotEqual;
            default:
                return vk::CompareOp::eAlways;
            }
        }

        vk::StencilOp ToVkStencilOperation(StencilOperation operation) {
            switch (operation) {
            case StencilOperation::Zero:
                return vk::StencilOp::eZero;
            case StencilOperation::Replace:
                return vk::StencilOp::eReplace;
            case StencilOperation::IncrClamp:
                return vk::StencilOp::eIncrementAndClamp;
            case StencilOperation::DecrClamp:
                return vk::StencilOp::eDecrementAndClamp;
            case StencilOperation::Invert:
                return vk::StencilOp::eInvert;
            case StencilOperation::IncrWrap:
                return vk::StencilOp::eIncrementAndWrap;
            case StencilOperation::DecrWrap:
                return vk::StencilOp::eDecrementAndWrap;
            case StencilOperation::Keep:
            default:
                return vk::StencilOp::eKeep;
            }
        }

        vk::StencilOpState ToVkStencil(const StencilState &state) {
            return vk::StencilOpState{
                ToVkStencilOperation(state.fail_operation),
                ToVkStencilOperation(state.pass_operation),
                ToVkStencilOperation(state.depth_fail_operation),
                ToVkCompareOperation(state.compare),
                state.compare_mask,
                state.write_mask,
                state.reference
            };
        }

        vk::ColorComponentFlags ToVkColorComponentMask(ColorChannelMask mask) {
            vk::ColorComponentFlags flags{};
            const auto raw = static_cast<int>(mask);
            if ((raw & static_cast<int>(ColorChannelMask::R)) != 0) flags |= vk::ColorComponentFlagBits::eR;
            if ((raw & static_cast<int>(ColorChannelMask::G)) != 0) flags |= vk::ColorComponentFlagBits::eG;
            if ((raw & static_cast<int>(ColorChannelMask::B)) != 0) flags |= vk::ColorComponentFlagBits::eB;
            if ((raw & static_cast<int>(ColorChannelMask::A)) != 0) flags |= vk::ColorComponentFlagBits::eA;
            return flags;
        }
    } // namespace

    VulkanPipelineState::VulkanPipelineState(const PipelineDescription &description) {
        m_vertex_bindings.reserve(description.vertex_bindings.size());
        for (size_t i = 0; i < description.vertex_bindings.size(); i++) {
            const VertexBinding &binding = description.vertex_bindings[i];
            m_vertex_bindings.emplace_back(
                static_cast<uint32_t>(i),
                binding.stride,
                binding.per_instance ? vk::VertexInputRate::eInstance : vk::VertexInputRate::eVertex
            );
        }

        m_vertex_attributes.reserve(description.vertex_attributes.size());
        for (const VertexAttributeDescription &attribute : description.vertex_attributes) {
            m_vertex_attributes.emplace_back(
                attribute.location, attribute.binding, ToVkVertexFormat(attribute.format), attribute.offset
            );
        }

        m_vertex_input = vk::PipelineVertexInputStateCreateInfo{
            vk::PipelineVertexInputStateCreateFlags{}, m_vertex_bindings, m_vertex_attributes
        };

        // Triangle lists with a dynamic viewport and scissor are what every
        // pipeline in this engine uses; the description does not state them
        // because nothing would ever set them differently.
        m_input_assembly = vk::PipelineInputAssemblyStateCreateInfo{
            {}, vk::PrimitiveTopology::eTriangleList, vk::False
        };

        const RasterizationState &raster = description.rasterization;
        m_rasterization = vk::PipelineRasterizationStateCreateInfo{
            {},
            vk::False,
            vk::False,
            ToVkPolygonMode(raster.fill),
            ToVkCullMode(raster.cull),
            ToVkFrontFace(raster.front_face),
            vk::False,
            0.0f, 0.0f, 0.0f,
            raster.line_width
        };

        const DepthStencilState &depth = description.depth_stencil;
        m_depth_stencil.depthTestEnable = depth.depth_test_enabled ? vk::True : vk::False;
        m_depth_stencil.depthWriteEnable = depth.depth_write_enabled ? vk::True : vk::False;
        m_depth_stencil.depthCompareOp = ToVkCompareOperation(depth.depth_compare);
        m_depth_stencil.depthBoundsTestEnable = depth.depth_bounds_test_enabled ? vk::True : vk::False;
        m_depth_stencil.stencilTestEnable = depth.stencil_test_enabled ? vk::True : vk::False;
        m_depth_stencil.front = ToVkStencil(depth.stencil_front);
        m_depth_stencil.back = ToVkStencil(depth.stencil_back);
        m_depth_stencil.minDepthBounds = depth.min_depth;
        m_depth_stencil.maxDepthBounds = depth.max_depth;

        m_blend_attachments.resize(description.color_formats.size());
        for (size_t i = 0; i < m_blend_attachments.size(); i++) {
            if (i >= description.color_blends.size()) {
                // A target the description does not mention is not written to at
                // all: a zero state writes no channel, which is the behaviour a
                // target with no blending entry has always had here.
                m_blend_attachments[i] = vk::PipelineColorBlendAttachmentState{};
                continue;
            }
            const ColorBlendState &blend = description.color_blends[i];
            m_blend_attachments[i] = vk::PipelineColorBlendAttachmentState{
                blend.enabled ? vk::True : vk::False,
                ToVkBlendFactor(blend.source_color_factor),
                ToVkBlendFactor(blend.destination_color_factor),
                ToVkBlendOperation(blend.color_operation),
                ToVkBlendFactor(blend.source_alpha_factor),
                ToVkBlendFactor(blend.destination_alpha_factor),
                ToVkBlendOperation(blend.alpha_operation),
                ToVkColorComponentMask(blend.write_mask)
            };
        }
        m_color_blend = vk::PipelineColorBlendStateCreateInfo{{}, vk::False, vk::LogicOp::eCopy, m_blend_attachments, {}};

        m_color_formats.reserve(description.color_formats.size());
        for (const ImageFormat format : description.color_formats) {
            m_color_formats.push_back(GetVkFormat(format));
        }
        m_rendering.colorAttachmentCount = static_cast<uint32_t>(m_color_formats.size());
        m_rendering.pColorAttachmentFormats = m_color_formats.data();
        m_rendering.depthAttachmentFormat = GetVkFormat(description.depth_format);
        m_rendering.stencilAttachmentFormat = vk::Format::eUndefined;
    }
} // namespace Engine::Rhi





