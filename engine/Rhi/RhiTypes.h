#ifndef ENGINE_RHI_RHITYPES_INCLUDED
#define ENGINE_RHI_RHITYPES_INCLUDED

#include "Rhi/rhi_export.h"

#include <cstddef>
#include "Rhi/Device/MemoryAccessTypes.h"

#include <cstdint>
#include <vector>

namespace Engine::Rhi {
    /**
     * @brief Value types and opaque handles shared by every backend.
     *
     * Nothing here names a backend: the types are plain values, and a handle is
     * an opaque identity whose meaning belongs to whoever created it. A backend
     * implementation converts its own objects to and from these types at the
     * boundary of its implementation units, so a header that includes this one
     * compiles without any graphics API header on the include path.
     */
    namespace RhiTypes {
    } // namespace RhiTypes

    /**
     * @brief An opaque, backend-owned object identity.
     *
     * The value is the backend's own object, flattened to an integer. It is
     * meaningless outside the backend that issued it, is never dereferenced or
     * interpreted by portable code, and is only ever compared for equality.
     *
     * `Tag` gives the handle a distinct type per kind of object, so a buffer
     * handle cannot be passed where an image view handle is expected.
     *
     * @note The default value names no object.
     */
    template <typename Tag>
    class ObjectHandle {
    public:
        constexpr ObjectHandle() noexcept = default;
        /// @brief Whether this handle names an object.
        constexpr bool IsValid() const noexcept {
            return m_value != 0;
        }
        /// @brief Whether this handle names an object, for boolean contexts.
        constexpr explicit operator bool() const noexcept {
            return m_value != 0;
        }
        /**
         * @brief Get the flattened value, for hashing and backend conversion.
         *
         * Portable code uses this only to compare and to hash; interpreting it
         * is the issuing backend's privilege.
         */
        constexpr uintptr_t GetValue() const noexcept {
            return m_value;
        }

        friend constexpr bool operator==(ObjectHandle lhs, ObjectHandle rhs) noexcept {
            return lhs.m_value == rhs.m_value;
        }
        friend constexpr bool operator!=(ObjectHandle lhs, ObjectHandle rhs) noexcept {
            return !(lhs == rhs);
        }

        /**
         * @brief Wrap a backend object that has been flattened to an integer.
         *
         * Called by a backend when it returns one of its objects; never called
         * by portable code.
         */
        static constexpr ObjectHandle FromValue(uintptr_t value) noexcept {
            ObjectHandle handle;
            handle.m_value = value;
            return handle;
        }

    private:
        uintptr_t m_value{0};
    };

    /// @brief Tag types giving each handle kind its own type.
    struct BufferHandleTag;
    struct ImageHandleTag;
    struct ImageViewHandleTag;
    struct SamplerHandleTag;
    struct DescriptorSetHandleTag;
    struct DescriptorSetLayoutHandleTag;
    struct PipelineHandleTag;
    struct PipelineLayoutHandleTag;
    struct CommandBufferHandleTag;
    struct SemaphoreHandleTag;
    struct FenceHandleTag;
    struct InstanceHandleTag;
    struct DeviceHandleTag;
    struct PhysicalDeviceHandleTag;
    struct SurfaceHandleTag;

    /// @brief A buffer object.
    using BufferHandle = ObjectHandle<BufferHandleTag>;
    /// @brief An image object.
    using ImageHandle = ObjectHandle<ImageHandleTag>;
    /// @brief A view over an image.
    using ImageViewHandle = ObjectHandle<ImageViewHandleTag>;
    /// @brief A sampler object.
    using SamplerHandle = ObjectHandle<SamplerHandleTag>;
    /// @brief A descriptor set.
    using DescriptorSetHandle = ObjectHandle<DescriptorSetHandleTag>;
    /// @brief A descriptor set layout.
    using DescriptorSetLayoutHandle = ObjectHandle<DescriptorSetLayoutHandleTag>;
    /// @brief A pipeline object.
    using PipelineHandle = ObjectHandle<PipelineHandleTag>;
    /// @brief A pipeline layout.
    using PipelineLayoutHandle = ObjectHandle<PipelineLayoutHandleTag>;
    /// @brief A command buffer.
    using CommandBufferHandle = ObjectHandle<CommandBufferHandleTag>;
    /// @brief A timeline or binary semaphore.
    using SemaphoreHandle = ObjectHandle<SemaphoreHandleTag>;
    /// @brief A fence.
    using FenceHandle = ObjectHandle<FenceHandleTag>;
    /// @brief The graphics instance.
    using InstanceHandle = ObjectHandle<InstanceHandleTag>;
    /// @brief The logical device.
    using DeviceHandle = ObjectHandle<DeviceHandleTag>;
    /// @brief The selected physical device.
    using PhysicalDeviceHandle = ObjectHandle<PhysicalDeviceHandleTag>;
    /// @brief The presentation surface.
    using SurfaceHandle = ObjectHandle<SurfaceHandleTag>;

    /// @brief A width and height in pixels.
    struct Extent2D {
        uint32_t width{0};
        uint32_t height{0};

        bool operator==(const Extent2D &) const = default;
    };

    /// @brief A width, height and depth in pixels.
    struct Extent3D {
        uint32_t width{0};
        uint32_t height{0};
        uint32_t depth{1};

        bool operator==(const Extent3D &) const = default;
    };

    /// @brief An unsigned 2D offset.
    struct Offset2D {
        int32_t x{0};
        int32_t y{0};

        bool operator==(const Offset2D &) const = default;
    };

    /// @brief A signed 3D offset.
    struct Offset3D {
        int32_t x{0};
        int32_t y{0};
        int32_t z{0};

        bool operator==(const Offset3D &) const = default;
    };

    /// @brief A rectangle: an offset and an extent.
    struct Rect2D {
        Offset2D offset{};
        Extent2D extent{};

        bool operator==(const Rect2D &) const = default;
    };

    /// @brief Dimensionality of an image or a view.
    enum class ImageType : uint8_t {
        D1,
        D2,
        D3
    };

    /// @brief The shape a view presents over its image.
    enum class ImageViewType : uint8_t {
        D1,
        D2,
        D3,
        Cube,
        D1Array,
        D2Array,
        CubeArray
    };

    /// @brief How many samples per pixel an image holds.
    enum class SampleCountBits : uint8_t {
        C1 = 1,
        C2 = 2,
        C4 = 4,
        C8 = 8,
        C16 = 16,
        C32 = 32,
        C64 = 64
    };

    /// @brief How a resource is seen by the shader that consumes it.
    enum class DescriptorType : uint8_t {
        /// A uniform buffer, read-only in the shader.
        UniformBuffer,
        /// A uniform buffer whose binding also takes a dynamic byte offset.
        UniformBufferDynamic,
        /// A storage buffer, readable and writable in the shader.
        StorageBuffer,
        /// A storage buffer whose binding also takes a dynamic byte offset.
        StorageBufferDynamic,
        /// A texture with its sampler folded in.
        CombinedImageSampler,
        /// A texture sampled through a separate sampler.
        SampledImage,
        /// A sampler alone.
        Sampler,
        /// A storage image, readable and writable in the shader.
        StorageImage
    };

    /**
     * @brief The layout a resource is in while a recorder is writing commands.
     *
     * This is portable vocabulary for the states a resource passes through, not
     * a barrier description: a backend that does not need layouts ignores it.
     */
    enum class ResourceState : uint8_t {
        /// No layout has been established yet.
        Undefined,
        /// General purpose, usable for writes.
        General,
        /// Written as a color attachment.
        ColorAttachment,
        /// Written as a depth/stencil attachment.
        DepthStencilAttachment,
        /// Read by a shader.
        ShaderReadOnly,
        /// Read as the source of a transfer.
        TransferSource,
        /// Written as the destination of a transfer.
        TransferDestination,
        /// Presented.
        PresentSource
    };

    /**
     * @brief One entry of a descriptor-set layout.
     *
     * A layout is a list of these: what each binding slot accepts and how many
     * of them it holds. It is the portable description a backend turns into its
     * own layout object, and the only place the engine states a set's shape.
     */
    struct DescriptorBinding {
        /// @brief Binding number within the set.
        uint32_t binding{0};
        /// @brief What the binding accepts.
        DescriptorType type{DescriptorType::UniformBuffer};
        /// @brief How many descriptors the binding holds.
        uint32_t count{1};
        /// @brief Sampler fixed into the layout, when the binding carries one.
        ///
        /// An immutable sampler belongs to the layout rather than to a bound
        /// resource: every set created from the layout sees this sampler, and no
        /// caller can substitute another. Both backends express it, so it is part
        /// of the portable description and not a Vulkan detail.
        SamplerHandle immutable_sampler{};

        bool operator==(const DescriptorBinding &) const = default;
    };

    /// @brief The portable description of a descriptor-set layout.
    using DescriptorSetLayout = std::vector<DescriptorBinding>;

    /// @brief One push-constant range of a pipeline layout.
    struct PushConstantRange {
        /// @brief Byte offset of the range within the block.
        uint32_t offset{0};
        /// @brief Byte size of the range.
        uint32_t size{0};
        /// @brief Whether the range is read by compute rather than graphics.
        bool compute{false};

        bool operator==(const PushConstantRange &) const = default;
    };

    /**
     * @brief What the device on this machine can do.
     *
     * Backends differ in their limits, so code that has to stay inside them asks
     * the device rather than assuming a number. Only the values the engine's own
     * work depends on are here; anything a backend enforces silently stays out.
     */
    struct DeviceCapabilities {
        /// @brief Largest push-constant block a pipeline layout may declare.
        uint32_t max_push_constant_bytes{0};
        /// @brief Alignment a dynamic uniform offset must be a multiple of.
        uint64_t uniform_buffer_offset_alignment{1};
        /// @brief Largest byte range a single storage-buffer binding covers.
        uint64_t max_storage_buffer_range{0};
        /// @brief Largest number of invocations in one workgroup.
        uint32_t max_compute_invocations{0};
    };

    /// @brief A timeline signal: a semaphore and the value it reaches.
    struct TimelineSignal {
        /// @brief The semaphore to signal.
        SemaphoreHandle semaphore{};
        /// @brief The value the semaphore is signaled to.
        uint64_t value{0};
    };

    /// @brief The portable description of a pipeline layout.
    struct PipelineLayoutDescription {
        /// @brief Set layouts, in binding order.
        std::vector<DescriptorSetLayoutHandle> set_layouts;
        /// @brief Constant ranges the pipeline reads.
        std::vector<PushConstantRange> push_constant_ranges;
    };

    /// @brief Which parts of an image a barrier or a copy covers.
    enum class ImageAspect : uint8_t {
        Color = 1 << 0,
        Depth = 1 << 1,
        Stencil = 1 << 2
    };

    /**
     * @brief What kind of work a transition separates.
     *
     * An access description says what a resource is read or written as; it does
     * not say what kind of pass does the reading. A barrier needs both: a
     * shadow-map atlas rewritten mid-frame is only ordered correctly if the
     * transition knows that graphics work follows it, while a compute dispatch
     * needs a narrower stage. `Unspecified` = "let the backend widen it", which is
     * what a pass with no work of its own leaves behind.
     */
    enum class PipelineScope : uint8_t {
        /// @brief The pass records no work of its own; the backend guesses.
        Unspecified,
        /// @brief Rasterisation, attachment and shader work.
        Graphics,
        /// @brief A compute dispatch.
        Compute,
        /// @brief A copy, upload or blit.
        Transfer
    };

    /**
     * @brief One image's transition between two access descriptions.
     *
     * The part of the image is named explicitly because a pass may write one mip
     * or one array layer while leaving the rest alone; Vulkan takes a subresource
     * range and WebGPU takes the subresource the transition applies to.
     */
    struct ImageBarrier {
        /// @brief The image being transitioned.
        ImageHandle image{};
        /// @brief Which aspects of the image the transition covers.
        ImageAspect aspect{ImageAspect::Color};
        /// @brief First mip level covered.
        uint32_t base_mip{0};
        /// @brief Number of mip levels covered.
        uint32_t mip_count{1};
        /// @brief First array layer covered.
        uint32_t base_layer{0};
        /// @brief Number of array layers covered.
        uint32_t layer_count{1};
        /// @brief How the image was accessed before this point.
        MemoryAccessTypeImage from{};
        /// @brief How the image is accessed after this point.
        MemoryAccessTypeImage to{};
        /// @brief The kind of work that wrote the image before this point.
        PipelineScope from_scope{PipelineScope::Unspecified};
        /// @brief The kind of work that reads the image after this point.
        PipelineScope to_scope{PipelineScope::Unspecified};
    };

    /**
     * @brief One box of an image copied into one box of another.
     *
     * A blit names a box in the source and a box in the destination; when the
     * sizes differ the copy is scaled. The layers are named as a range because
     * that is what both APIs take, and a cube-face copy is exactly this shape.
     */
    struct ImageBlitDescription {
        /// @brief The image being read.
        ImageHandle source{};
        /// @brief The image being written.
        ImageHandle destination{};
        /// @brief Which aspects of both images the copy covers.
        ImageAspect aspect{ImageAspect::Color};
        /// @brief Source mip level.
        uint32_t source_mip{0};
        /// @brief First source array layer.
        uint32_t source_layer{0};
        /// @brief Number of source array layers.
        uint32_t source_layer_count{1};
        /// @brief Destination mip level.
        uint32_t destination_mip{0};
        /// @brief First destination array layer.
        uint32_t destination_layer{0};
        /// @brief Number of destination array layers.
        uint32_t destination_layer_count{1};
        /// @brief Inclusive lower corner of the source box.
        Offset3D source_min{};
        /// @brief Exclusive upper corner of the source box.
        Offset3D source_max{};
        /// @brief Inclusive lower corner of the destination box.
        Offset3D destination_min{};
        /// @brief Exclusive upper corner of the destination box.
        Offset3D destination_max{};
    };

    /**
     * @brief A capability a format may or may not have on the current device.
     *
     * The set is what the engine asks about: whether a format can back a vertex
     * buffer, and whether it can be a storage image with or without atomics.
     */
    enum class FormatFeature : uint8_t {
        /// The format can be read as vertex attribute data.
        VertexBuffer,
        /// The format can be a shader storage image.
        StorageImage,
        /// The format can be a shader storage image with atomic operations.
        StorageImageAtomic
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_RHITYPES_INCLUDED









