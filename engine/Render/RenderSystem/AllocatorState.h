#ifndef RENDER_RENDERSYSTEM_ALLOCATORSTATE_INCLUDED
#define RENDER_RENDERSYSTEM_ALLOCATORSTATE_INCLUDED

#include <memory>
#include <vulkan/vulkan.h>
#include "Render/ImageUtils.h"
#include "Render/Memory/AllocatedMemory.h"

class VkExtent3D;
class VmaAllocator_T;
typedef VmaAllocator_T* VmaAllocator;


namespace vk {
    enum class Format;
    enum class ImageType;
    enum class SampleCountFlagBits : uint32_t;
    enum class FormatFeatureFlagBits : uint32_t;

    class Extent3D;
}

namespace Engine {
    class RenderSystem;
    namespace RenderSystemState {
        class AllocatorState {
        public:
            enum class BufferType {
                // Staging buffer on host memory
                Staging,
                // Readback
                Readback,
                // Vertex buffer on device memory
                Vertex,
                // Uniform buffer on host memory
                Uniform
            };

        private:
            struct impl;
            std::unique_ptr <impl> pimpl;

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

            BufferAllocation AllocateBuffer(
                BufferType type,
                size_t size,
                const std::string &name = ""
            ) const;

            std::unique_ptr<BufferAllocation> AllocateBufferUnique(
                BufferType type, size_t size, const std::string &name = ""
            ) const noexcept;

            ImageAllocation AllocateImage(
                ImageUtils::ImageType type,
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
                ImageUtils::ImageType type,
                vk::ImageType dimension,
                vk::Extent3D extent,
                vk::Format format,
                uint32_t miplevel,
                uint32_t array_layers,
                bool is_cube_map,
                vk::SampleCountFlagBits samples,
                const std::string &name = ""
            ) const noexcept;

            bool QueryFormatFeatures(
                vk::Format format,
                vk::FormatFeatureFlagBits feature
            ) const noexcept;
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RENDERSYSTEM_ALLOCATORSTATE_INCLUDED
