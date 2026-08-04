#ifndef GPU_CONTEXT_ALLOCATORSTATE_INCLUDED
#define GPU_CONTEXT_ALLOCATORSTATE_INCLUDED

#include "MemoryAllocation.h"
#include "MemoryTypes.h"
#include "gpu_context_export.h"
#include <memory>
#include <vulkan/vulkan.hpp>

class VkExtent3D;
class VmaAllocator_T;
typedef VmaAllocator_T *VmaAllocator;

namespace Engine {
    namespace RenderSystemState {
        class DeviceInterface;

        /**
         * @brief State of the underlying memory allocator.
         *
         * Currently the allocator is implemented via the VMA library.
         */
        class GPU_CONTEXT_API AllocatorState {
            struct impl;
            std::unique_ptr<impl> pimpl;

            DeviceInterface *m_device_interface = nullptr;

        public:
            AllocatorState();
            void SetDeviceInterface(DeviceInterface &device_interface);

            AllocatorState(const AllocatorState &) = delete;
            AllocatorState &operator=(const AllocatorState &) = delete;

            AllocatorState(AllocatorState &&) = default;

            ~AllocatorState();
            /// @brief Create the allocator state by initializing the VMA library.
            void Create();
            /// @brief Get the underlying allocator state.
            VmaAllocator GetAllocator() const;

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
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RENDERSYSTEM_ALLOCATORSTATE_INCLUDED
