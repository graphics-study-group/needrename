#ifndef ENGINE_RHI_RHITYPES_INCLUDED
#define ENGINE_RHI_RHITYPES_INCLUDED

#include "Rhi/rhi_export.h"

#include <cstddef>
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

        bool operator==(const DescriptorBinding &) const = default;
    };

    /// @brief The portable description of a descriptor-set layout.
    using DescriptorSetLayout = std::vector<DescriptorBinding>;

    /// @brief Which parts of an image a barrier or a copy covers.
    enum class ImageAspect : uint8_t {
        Color = 1 << 0,
        Depth = 1 << 1,
        Stencil = 1 << 2
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


