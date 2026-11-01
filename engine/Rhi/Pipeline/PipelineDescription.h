#ifndef ENGINE_RHI_PIPELINEDESCRIPTION_INCLUDED
#define ENGINE_RHI_PIPELINEDESCRIPTION_INCLUDED

#include "Rhi/Pipeline/PipelineEnums.h"
#include "Rhi/RhiTypes.h"
#include "Rhi/Texture/ImageUtils.h"

#include <cstdint>
#include <vector>

namespace Engine::Rhi {
    /**
     * @brief The shape of one vertex attribute's components.
     *
     * The set is the one a GPU actually accepts as vertex input: 32-bit floats
     * and unsigned integers in one to four components, and the two packed forms
     * meshes carry. A backend maps each to whatever it calls that format.
     */
    enum class VertexFormat : uint8_t {
        /// @brief No data.
        Undefined,
        /// @brief One 32-bit float.
        Float32x1,
        /// @brief Two 32-bit floats.
        Float32x2,
        /// @brief Three 32-bit floats.
        Float32x3,
        /// @brief Four 32-bit floats.
        Float32x4,
        /// @brief Four 8-bit unsigned integers in one word.
        UInt8x4,
        /// @brief Two 16-bit unsigned integers in one word.
        UInt16x2,
        /// @brief One 32-bit unsigned integer.
        UInt32x1
    };

    /// @brief One vertex buffer binding of a pipeline.
    struct VertexBinding {
        /// @brief Distance in bytes between consecutive elements.
        uint32_t stride{0};
        /// @brief Whether the buffer advances per instance rather than per vertex.
        bool per_instance{false};

        bool operator==(const VertexBinding &) const = default;
    };

    /// @brief One vertex attribute read from a binding.
    struct VertexAttributeDescription {
        /// @brief Shader location the attribute feeds.
        uint32_t location{0};
        /// @brief Binding the attribute is read from.
        uint32_t binding{0};
        /// @brief Shape of the attribute.
        VertexFormat format{VertexFormat::Undefined};
        /// @brief Byte offset within the element.
        uint32_t offset{0};

        bool operator==(const VertexAttributeDescription &) const = default;
    };

    /// @brief How a colour target is blended into.
    struct ColorBlendState {
        /// @brief Whether blending is applied at all.
        bool enabled{false};
        /// @brief Operation combining source and destination.
        BlendOperation color_operation{BlendOperation::Add};
        /// @brief Factor applied to the source colour.
        BlendFactor source_color_factor{BlendFactor::One};
        /// @brief Factor applied to the destination colour.
        BlendFactor destination_color_factor{BlendFactor::Zero};
        /// @brief Operation combining source and destination alpha.
        BlendOperation alpha_operation{BlendOperation::Add};
        /// @brief Factor applied to the source alpha.
        BlendFactor source_alpha_factor{BlendFactor::One};
        /// @brief Factor applied to the destination alpha.
        BlendFactor destination_alpha_factor{BlendFactor::Zero};
        /// @brief Channels the pipeline may write.
        ColorChannelMask write_mask{ColorChannelMask::RGBA};

        bool operator==(const ColorBlendState &) const = default;
    };

    /// @brief How primitives are assembled and rasterised.
    struct RasterizationState {
        /// @brief How triangles are filled.
        FillingMode fill{FillingMode::Fill};
        /// @brief Which faces are discarded.
        CullingMode cull{CullingMode::None};
        /// @brief Winding that counts as front-facing.
        FrontFace front_face{FrontFace::Counterclockwise};
        /// @brief Whether to draw lines with a width other than one.
        float line_width{1.0f};

        bool operator==(const RasterizationState &) const = default;
    };

    /// @brief How depth and stencil tests are applied.
    struct DepthStencilState {
        /// @brief Whether the depth test runs.
        bool depth_test_enabled{false};
        /// @brief Whether passing depth writes it.
        bool depth_write_enabled{false};
        /// @brief Comparison used by the depth test.
        DSComparator depth_compare{DSComparator::Less};
        /// @brief Whether the stencil test runs.
        bool stencil_test_enabled{false};

        bool operator==(const DepthStencilState &) const = default;
    };

    /**
     * @brief A graphics pipeline, described without naming a backend.
     *
     * The four groups below are what Vulkan passes as four separate create-info
     * structures and what WebGPU passes as one descriptor. Naming them together
     * is what lets the render layer state a pipeline once.
     */
    struct PipelineDescription {
        /// @brief Vertex buffers the pipeline reads.
        std::vector<VertexBinding> vertex_bindings;
        /// @brief Vertex attributes the pipeline reads.
        std::vector<VertexAttributeDescription> vertex_attributes;
        /// @brief Primitive assembly and rasterisation.
        RasterizationState rasterization;
        /// @brief Depth and stencil behaviour.
        DepthStencilState depth_stencil;
        /// @brief Blending, one entry per colour target.
        std::vector<ColorBlendState> color_blends;
        /// @brief Formats of the colour targets the pipeline writes.
        std::vector<ImageFormat> color_formats;
        /// @brief Format of the depth target, or `UNDEFINED` when there is none.
        ImageFormat depth_format{ImageFormat::UNDEFINED};
    };

    /// @brief What a pass does to one attachment when it begins.
    enum class LoadOperation : uint8_t {
        /// @brief The previous contents are kept.
        Load,
        /// @brief The attachment is cleared to the pass's clear value.
        Clear,
        /// @brief The previous contents are not needed.
        DontCare
    };

    /// @brief What a pass leaves in one attachment when it ends.
    enum class StoreOperation : uint8_t {
        /// @brief The contents are kept for later use.
        Store,
        /// @brief The contents are not needed afterwards.
        DontCare
    };

    /// @brief A colour value a pass clears to.
    struct ClearColor {
        /// @brief Red channel.
        float r{0.0f};
        /// @brief Green channel.
        float g{0.0f};
        /// @brief Blue channel.
        float b{0.0f};
        /// @brief Alpha channel.
        float a{1.0f};

        bool operator==(const ClearColor &) const = default;
    };

    /// @brief One colour attachment of a pass.
    struct ColorAttachmentDescription {
        /// @brief The image view written.
        ImageViewHandle view{};
        /// @brief What happens to the previous contents.
        LoadOperation load{LoadOperation::Clear};
        /// @brief What happens to the result.
        StoreOperation store{StoreOperation::Store};
        /// @brief Value the attachment is cleared to when loading clears.
        ClearColor clear{};
    };

    /// @brief The depth attachment of a pass.
    struct DepthAttachmentDescription {
        /// @brief The image view written.
        ImageViewHandle view{};
        /// @brief What happens to the previous contents.
        LoadOperation load{LoadOperation::Clear};
        /// @brief What happens to the result.
        StoreOperation store{StoreOperation::Store};
        /// @brief Value the attachment is cleared to when loading clears.
        float clear_depth{1.0f};
    };

    /**
     * @brief One pass over existing images.
     *
     * Vulkan binds attachments per pass and WebGPU fixes them for a pass's
     * duration; both are the same statement in these terms, which is why the
     * description names no backend and carries no handle to a render pass object.
     */
    struct RenderPassDescription {
        /// @brief Colour attachments, in binding order.
        std::vector<ColorAttachmentDescription> color_attachments;
        /// @brief The depth attachment; ignored when its view is invalid.
        DepthAttachmentDescription depth_attachment;
        /// @brief Area the pass covers.
        Rect2D area{};
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_PIPELINEDESCRIPTION_INCLUDED



