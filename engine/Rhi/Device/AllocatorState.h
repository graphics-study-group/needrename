#ifndef ENGINE_RHI_ALLOCATORSTATE_INCLUDED
#define ENGINE_RHI_ALLOCATORSTATE_INCLUDED

#include "Rhi/Device/MemoryAllocation.h"
#include "Rhi/Device/MemoryTypes.h"
#include "Rhi/rhi_export.h"
#include <memory>
#include <vulkan/vulkan.hpp>

class VkExtent3D;
struct VmaAllocator_T;
typedef VmaAllocator_T *VmaAllocator;

namespace Engine::Rhi {
    class DeviceInterface;
    class EpochTracker;

    /**
     * @brief State of the underlying memory allocator.
     *
     * Currently the allocator is implemented via the VMA library.
     *
     * @note An epoch tracker may be installed on the allocator. When one is,
     * every buffer allocation made through it hands itself to that tracker on
     * destruction instead of freeing device memory. Without a tracker,
     * allocations destroy themselves immediately, which is the semantics a
     * setup that never defers work wants.
     */
    class RHI_API AllocatorState {
        struct impl;
        std::unique_ptr<impl> pimpl;

        DeviceInterface *m_device_interface = nullptr;

    public:
        /**
         * @brief Construct the allocator state, initializing the VMA library
         * on the given device.
         */
        explicit AllocatorState(DeviceInterface &device_interface);

        AllocatorState(const AllocatorState &) = delete;
        AllocatorState &operator=(const AllocatorState &) = delete;

        AllocatorState(AllocatorState &&) noexcept;

        ~AllocatorState();
        /// @brief Get the underlying allocator state.
        VmaAllocator GetAllocator() const;

        /**
         * @brief Install the epoch tracker that receives retired buffer allocations.
         *
         * A setter rather than a constructor parameter, so that
         * `AllocatorState(DeviceInterface &)` stays intact for setups that build
         * an allocator without a retirement facility.
         *
         * @param sink The tracker, or null to restore immediate destruction. The
         * caller retains ownership and must keep the tracker alive for as long
         * as any allocation made through this allocator can be destroyed.
         */
        void SetRetireSink(EpochTracker *sink) noexcept;

        /**
         * @brief Get the installed epoch tracker, or null when none is installed.
         */
        EpochTracker *GetRetireSink() const noexcept;

        /**
         * @brief Allocate the memory for buffer of a given type, name and size.
         */
        BufferAllocation AllocateBuffer(BufferType type, size_t size, const std::string &name = "") const;

        /**
         * @overload BufferAllocation AllocatorState::AllocateBufferUnique()
         *
         * This variant creates a unique pointer instead, and does not throw
         * exceptions.
         *
         * @see ImageAllocation AllocatorState::AllocateBuffer()
         */
        std::unique_ptr<BufferAllocation> AllocateBufferUnique(
            BufferType type, size_t size, const std::string &name = ""
        ) const noexcept;

        /**
         * @brief Description of image allocation request.
         */
        struct ImageAllocationDescription {
            ImageMemoryType type;            ///< Supported memory access pattern of the image.
            vk::ImageType dimension;         ///< Dimension of the image. 1D, 2D or 3D are supported.
            vk::Extent3D extent;             ///< Extent of the image. Must be non-zero integers.
            vk::Format format;               ///< Format of the image.
            uint32_t miplevel;               ///< Mipmap levels of the image.
            uint32_t array_layers;           ///< Array layers of the image. 3D arrays are not supported.
            bool is_cube_map;                ///< Whether the array allocation should be compatible to cubemaps.
            vk::SampleCountFlagBits samples; ///< Multisample counts of the image.
        };

        /**
         * @brief Allocate the memory for a given image.
         */
        ImageAllocation AllocateImage(const ImageAllocationDescription &desc, const std::string &name = "") const;

        /**
         * @overload ImageAllocation AllocatorState::AllocateImageUnique()
         *
         * This variant creates a unique pointer instead, and does not throw
         * exceptions.
         *
         * @see ImageAllocation AllocatorState::AllocateImage()
         */
        std::unique_ptr<ImageAllocation> AllocateImageUnique(
            const ImageAllocationDescription &desc, const std::string &name = ""
        ) const noexcept;

        /**
         * @brief Query whether a given format supports intended usage feature.
         */
        bool QueryFormatFeatures(vk::Format format, vk::FormatFeatureFlagBits feature) const noexcept;
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_ALLOCATORSTATE_INCLUDED
