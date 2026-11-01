#include "Rhi/Pipeline/Pipeline.h"

#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Pipeline/PipelineBuilder.h"
#include "Rhi/Resource/ImmutableResourceCache.h"

#include <SDL3/SDL.h>
#include <webgpu/webgpu.h>

namespace Engine::Rhi {
    namespace {
        WGPUVertexFormat ToWgpuVertexFormat(VertexFormat format) {
            switch (format) {
            case VertexFormat::Float32x1:
                return WGPUVertexFormat_Float32;
            case VertexFormat::Float32x2:
                return WGPUVertexFormat_Float32x2;
            case VertexFormat::Float32x3:
                return WGPUVertexFormat_Float32x3;
            case VertexFormat::Float32x4:
                return WGPUVertexFormat_Float32x4;
            case VertexFormat::UInt8x4:
                return WGPUVertexFormat_Uint8x4;
            case VertexFormat::UInt16x2:
                return WGPUVertexFormat_Uint16x2;
            case VertexFormat::UInt32x1:
                return WGPUVertexFormat_Uint32;
            case VertexFormat::Undefined:
            default:
                // The standard interface has no "undefined" vertex format; the caller
                // describing an attribute without one gets an unusable layout either way.
                return WGPUVertexFormat_Float32;
            }
        }

        WGPUCompareFunction ToWgpuCompare(DSComparator comparator) {
            switch (comparator) {
            case DSComparator::Never:
                return WGPUCompareFunction_Never;
            case DSComparator::Less:
                return WGPUCompareFunction_Less;
            case DSComparator::Equal:
                return WGPUCompareFunction_Equal;
            case DSComparator::LEqual:
                return WGPUCompareFunction_LessEqual;
            case DSComparator::Greater:
                return WGPUCompareFunction_Greater;
            case DSComparator::NEqual:
                return WGPUCompareFunction_NotEqual;
            case DSComparator::GEqual:
                return WGPUCompareFunction_GreaterEqual;
            case DSComparator::Always:
            default:
                return WGPUCompareFunction_Always;
            }
        }

        WGPUBlendFactor ToWgpuBlendFactor(BlendFactor factor) {
            switch (factor) {
            case BlendFactor::Zero:
                return WGPUBlendFactor_Zero;
            case BlendFactor::One:
                return WGPUBlendFactor_One;
            case BlendFactor::SrcColor:
                return WGPUBlendFactor_Src;
            case BlendFactor::OneMinusSrcColor:
                return WGPUBlendFactor_OneMinusSrc;
            case BlendFactor::SrcAlpha:
                return WGPUBlendFactor_SrcAlpha;
            case BlendFactor::OneMinusSrcAlpha:
                return WGPUBlendFactor_OneMinusSrcAlpha;
            case BlendFactor::DstColor:
                return WGPUBlendFactor_Dst;
            case BlendFactor::OneMinusDstColor:
                return WGPUBlendFactor_OneMinusDst;
            case BlendFactor::DstAlpha:
                return WGPUBlendFactor_DstAlpha;
            case BlendFactor::OneMinusDstAlpha:
            default:
                return WGPUBlendFactor_OneMinusDstAlpha;
            }
        }

        WGPUBlendOperation ToWgpuBlendOperation(BlendOperation operation) {
            switch (operation) {
            case BlendOperation::Substract:
                return WGPUBlendOperation_Subtract;
            case BlendOperation::ReverseSubstract:
                return WGPUBlendOperation_ReverseSubtract;
            case BlendOperation::None:
                return WGPUBlendOperation_Add;
            case BlendOperation::Min:
                return WGPUBlendOperation_Min;
            case BlendOperation::Max:
                return WGPUBlendOperation_Max;
            case BlendOperation::Add:
            default:
                return WGPUBlendOperation_Add;
            }
        }

        WGPUColorWriteMask ToWgpuWriteMask(ColorChannelMask mask) {
            const auto bits = static_cast<int>(mask);
            WGPUColorWriteMask result = WGPUColorWriteMask_None;
            if (bits & static_cast<int>(ColorChannelMask::R)) {
                result |= WGPUColorWriteMask_Red;
            }
            if (bits & static_cast<int>(ColorChannelMask::G)) {
                result |= WGPUColorWriteMask_Green;
            }
            if (bits & static_cast<int>(ColorChannelMask::B)) {
                result |= WGPUColorWriteMask_Blue;
            }
            if (bits & static_cast<int>(ColorChannelMask::A)) {
                result |= WGPUColorWriteMask_Alpha;
            }
            return result;
        }
    } // namespace

    /**
     * @brief A graphics pipeline built from the seam's description.
     *
     * The state groups translate almost field for field. The exceptions are worth naming:
     *
     * - **Specialization constants have no equivalent.** A value baked into the pipeline
     *   here is a value baked into the WGSL at compile time, which is what the bake does
     *   (D9). The description's entries are therefore ignored rather than approximated.
     * - **`line_width` and the depth-bias terms** are Vulkan's; the standard interface
     *   has no line width and expresses depth bias through `WGPUDepthStencilState`. The
     *   bias terms are passed through.
     * - **Stencil state** is not carried in the description's `depth_stencil` today beyond
     *   enabling it, so the pass keeps the standard interface's defaults.
     *
     * Vertex buffers are grouped by binding: the seam lists bindings and attributes
     * separately, and the standard interface wants the attributes nested under their
     * binding.
     */
    struct Pipeline::impl {
        WGPURenderPipeline pipeline{nullptr};
        WGPUComputePipeline compute_pipeline{nullptr};
    };

    Pipeline::Pipeline(std::unique_ptr<impl> p) noexcept : pimpl(std::move(p)) {
    }

    Pipeline Pipeline::Create(const DeviceContext &device_context, const GraphicsPipelineBuildDescription &description) {
        auto state = std::make_unique<impl>();
        const WGPUDevice device = ToWgpuDevice(device_context.GetDevice());

        // Group the attributes under the binding they read from.
        std::vector<std::vector<WGPUVertexAttribute>> attributes_by_binding(
            description.pipeline.vertex_bindings.size()
        );
        for (const VertexAttributeDescription &attribute : description.pipeline.vertex_attributes) {
            if (attribute.binding >= attributes_by_binding.size()) {
                SDL_LogError(
                    SDL_LOG_CATEGORY_RENDER,
                    "WebGPU: vertex attribute %u names binding %u, which the pipeline does not declare.",
                    attribute.location,
                    attribute.binding
                );
                continue;
            }
            attributes_by_binding[attribute.binding].push_back(
                WGPUVertexAttribute{
                    .format = ToWgpuVertexFormat(attribute.format),
                    .offset = attribute.offset,
                    .shaderLocation = attribute.location,
                }
            );
        }

        std::vector<WGPUVertexBufferLayout> vertex_buffers;
        vertex_buffers.reserve(description.pipeline.vertex_bindings.size());
        for (size_t i = 0; i < description.pipeline.vertex_bindings.size(); i++) {
            const VertexBinding &binding = description.pipeline.vertex_bindings[i];
            WGPUVertexBufferLayout layout{};
            layout.arrayStride = binding.stride;
            layout.stepMode = binding.per_instance ? WGPUVertexStepMode_Instance : WGPUVertexStepMode_Vertex;
            layout.attributeCount = attributes_by_binding[i].size();
            layout.attributes = attributes_by_binding[i].data();
            vertex_buffers.push_back(layout);
        }

        // The shaders, one per stage.
        const GraphicsPipelineStage *vertex_stage = nullptr;
        const GraphicsPipelineStage *fragment_stage = nullptr;
        for (const GraphicsPipelineStage &stage : description.stages) {
            switch (stage.stage) {
            case ShaderStage::Vertex:
                vertex_stage = &stage;
                break;
            case ShaderStage::Fragment:
                fragment_stage = &stage;
                break;
            default:
                SDL_LogError(
                    SDL_LOG_CATEGORY_RENDER,
                    "WebGPU: pipeline stage %d is not expressible here; only vertex and fragment are.",
                    static_cast<int>(stage.stage)
                );
                break;
            }
        }
        if (vertex_stage == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "WebGPU: a graphics pipeline needs a vertex stage.");
            return Pipeline(std::move(state));
        }

        WGPUVertexState vertex_state{};
        vertex_state.module = ToWgpuShaderModule(vertex_stage->module);
        vertex_state.entryPoint = WGPUStringView{vertex_stage->entry_point.data(), vertex_stage->entry_point.size()};
        vertex_state.bufferCount = vertex_buffers.size();
        vertex_state.buffers = vertex_buffers.data();

        // Colour targets, one per declared format.
        std::vector<WGPUColorTargetState> color_targets;
        std::vector<WGPUBlendState> blend_states;
        color_targets.reserve(description.pipeline.color_formats.size());
        blend_states.reserve(description.pipeline.color_formats.size());
        for (size_t i = 0; i < description.pipeline.color_formats.size(); i++) {
            const ColorBlendState blend = i < description.pipeline.color_blends.size()
                                              ? description.pipeline.color_blends[i]
                                              : ColorBlendState{};
            WGPUBlendState blend_state{};
            blend_state.color.operation = ToWgpuBlendOperation(blend.color_operation);
            blend_state.color.srcFactor = ToWgpuBlendFactor(blend.source_color_factor);
            blend_state.color.dstFactor = ToWgpuBlendFactor(blend.destination_color_factor);
            blend_state.alpha.operation = ToWgpuBlendOperation(blend.alpha_operation);
            blend_state.alpha.srcFactor = ToWgpuBlendFactor(blend.source_alpha_factor);
            blend_state.alpha.dstFactor = ToWgpuBlendFactor(blend.destination_alpha_factor);
            blend_states.push_back(blend_state);

            WGPUColorTargetState target{};
            target.format = ToWgpuTextureFormat(description.pipeline.color_formats[i]);
            target.blend = blend.enabled ? &blend_states.back() : nullptr;
            // The write mask is what the seam declares. A zero mask here is a pipeline that
            // writes nothing, which is a caller bug the caller can see rather than one this
            // backend should paper over.
            target.writeMask = ToWgpuWriteMask(blend.write_mask);
            color_targets.push_back(target);
        }

        WGPUFragmentState fragment_state{};
        const bool has_fragment = fragment_stage != nullptr && !color_targets.empty();
        if (has_fragment) {
            fragment_state.module = ToWgpuShaderModule(fragment_stage->module);
            fragment_state.entryPoint =
                WGPUStringView{fragment_stage->entry_point.data(), fragment_stage->entry_point.size()};
            fragment_state.targetCount = color_targets.size();
            fragment_state.targets = color_targets.data();
        }

        const RasterizationState &raster = description.pipeline.rasterization;
        WGPUPrimitiveState primitive{};
        primitive.topology = WGPUPrimitiveTopology_TriangleList;
        primitive.frontFace = raster.front_face == FrontFace::Clockwise ? WGPUFrontFace_CW : WGPUFrontFace_CCW;
        switch (raster.cull) {
        case CullingMode::Front:
            primitive.cullMode = WGPUCullMode_Front;
            break;
        case CullingMode::Back:
            primitive.cullMode = WGPUCullMode_Back;
            break;
        case CullingMode::All:
            // The standard interface has no "cull everything"; culling both is what a
            // caller asking for it gets when the pipeline still has to be creatable.
            primitive.cullMode = WGPUCullMode_Back;
            break;
        case CullingMode::None:
        default:
            primitive.cullMode = WGPUCullMode_None;
            break;
        }
        // `raster.fill` and `raster.line_width` have no counterpart: there is no polygon
        // mode and no wide lines. A caller that asks for either gets triangles.

        WGPUDepthStencilState depth_state{};
        const bool has_depth = description.pipeline.depth_format != ImageFormat::UNDEFINED;
        if (has_depth) {
            depth_state.format = ToWgpuTextureFormat(description.pipeline.depth_format);
            depth_state.depthWriteEnabled = description.pipeline.depth_stencil.depth_write_enabled
                                                ? WGPUOptionalBool_True
                                                : WGPUOptionalBool_False;
            depth_state.depthCompare = ToWgpuCompare(description.pipeline.depth_stencil.depth_compare);
            depth_state.depthBias = static_cast<int32_t>(raster.depth_bias_constant);
            depth_state.depthBiasSlopeScale = raster.depth_bias_slope;
            depth_state.depthBiasClamp = 0.0f;
            depth_state.stencilFront.compare = WGPUCompareFunction_Always;
            depth_state.stencilFront.failOp = WGPUStencilOperation_Keep;
            depth_state.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
            depth_state.stencilFront.passOp = WGPUStencilOperation_Keep;
            depth_state.stencilBack = depth_state.stencilFront;
            // Reading only needs the depth aspect when the stencil is not declared.
            depth_state.stencilReadMask = 0xFF;
            depth_state.stencilWriteMask = 0xFF;
        }

        WGPURenderPipelineDescriptor descriptor{};
        descriptor.layout = ToWgpuPipelineLayout(description.layout);
        descriptor.vertex = vertex_state;
        descriptor.fragment = has_fragment ? &fragment_state : nullptr;
        descriptor.primitive = primitive;
        // One sample: the seam states a sample count on the texture, not on the pipeline,
        // and this backend creates its textures single-sampled.
        descriptor.multisample.count = 1;
        descriptor.multisample.mask = 0xFFFFFFFF;
        descriptor.depthStencil = has_depth ? &depth_state : nullptr;

        state->pipeline = wgpuDeviceCreateRenderPipeline(device, &descriptor);
        if (state->pipeline == nullptr) {
            // The device reports why through its uncaptured-error callback; a null pipeline
            // is this level's summary. Specialization entries were ignored on purpose - see
            // the class comment.
            SDL_LogError(
                SDL_LOG_CATEGORY_RENDER,
                "WebGPU: render pipeline creation failed (%zu colour targets, depth %s).",
                color_targets.size(),
                has_depth ? "on" : "off"
            );
        }
        return Pipeline(std::move(state));
    }

    Pipeline::Pipeline(Pipeline &&) noexcept = default;
    Pipeline &Pipeline::operator=(Pipeline &&) noexcept = default;

    Pipeline::~Pipeline() {
        if (!pimpl) {
            return;
        }
        if (pimpl->pipeline != nullptr) {
            wgpuRenderPipelineRelease(pimpl->pipeline);
        }
        if (pimpl->compute_pipeline != nullptr) {
            wgpuComputePipelineRelease(pimpl->compute_pipeline);
        }
    }

    PipelineHandle Pipeline::GetHandle() const noexcept {
        if (pimpl->pipeline != nullptr) {
            return FromWgpuRenderPipeline(pimpl->pipeline);
        }
        // A compute pipeline is a different object type in the standard interface; the
        // seam's handle covers both, and a caller that binds one knows which it built.
        return PipelineHandle::FromValue(reinterpret_cast<uintptr_t>(pimpl->compute_pipeline));
    }

    bool Pipeline::IsValid() const noexcept {
        return pimpl->pipeline != nullptr || pimpl->compute_pipeline != nullptr;
    }
} // namespace Engine::Rhi


