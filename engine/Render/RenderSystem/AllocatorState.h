#ifndef RENDER_RENDERSYSTEM_ALLOCATORSTATE_INCLUDED
#define RENDER_RENDERSYSTEM_ALLOCATORSTATE_INCLUDED

#include "Render/ImageUtils.h"
#include "Render/Memory/AllocatedMemory.h"
#include "Render/Memory/MemoryTypes.h"
#include <memory>
#include <vulkan/vulkan.h>

class VkExtent3D;
class VmaAllocator_T;
typedef VmaAllocator_T *VmaAllocator;

namespace vk {
    enum class Format;
    enum class ImageType;
    enum class SampleCountFlagBits : uint32_t;
    enum class FormatFeatureFlagBits : uint32_t;

    class Extent3D;
} // namespace vk

namespace Engine {
    class RenderSystem;
    namespace RenderSystemState {

        /**
         * @brief State of the underlying memory allocator.
         *
         * Currently the allocator is implemented via the VMA library.
         */
        class AllocatorState {
            struct impl;
            std::unique_ptr<impl> pimpl;

            RenderSystem &m_system;

        public:
            AllocatorState(RenderSystem &system);

            AllocatorState(const AllocatorState &) = delete;
            AllocatorState(AllocatorState &&) = default;
            AllocatorState &operator=(const AllocatorState &) = delete;
            AllocatorState &operator=(AllocatorState &) = default;

            ~AllocatorState();

            void Create();
            VmaAllocator GetAllocator() const;

            /**
             * @brief Allocate the memory for buffer of a given type, name and size.
             */
            BufferAllocation AllocateBuffer(BufferType type, size_t size, const std::string &name = "") const;
            std::unique_ptr<BufferAllocation> AllocateBufferUnique(
                BufferType type, size_t size, const std::string &name = ""
            ) const noexcept;

            /**
             * @brief Allocate the memory for a given image.
             */
            ImageAllocation AllocateImage(
                ImageMemoryType type,
                vk::ImageType dimension,
                vk::Extent3D extent,
                vk::Format format,
                uint32_t miplevel,
                uint32_t array_layers,
                bool is_cube_map,
                vk::SampleCountFlagBits samples,
                const std::string &name = ""
            ) const;
            std::unique_ptr<ImageAllocation> AllocateImageUnique(
                ImageMemoryType type,
                vk::ImageType dimension,
                vk::Extent3D extent,
                vk::Format format,
                uint32_t miplevel,
                uint32_t array_layers,
                bool is_cube_map,
                vk::SampleCountFlagBits samples,
                const std::string &name = ""
            ) const noexcept;

            /**
             * @brief Query whether a given format supports intended usage feature.
             */
            bool QueryFormatFeatures(vk::Format format, vk::FormatFeatureFlagBits feature) const noexcept;
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RENDERSYSTEM_ALLOCATORSTATE_INCLUDED
