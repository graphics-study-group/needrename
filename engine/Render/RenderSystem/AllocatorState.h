#ifndef RENDER_RENDERSYSTEM_ALLOCATORSTATE_INCLUDED
#define RENDER_RENDERSYSTEM_ALLOCATORSTATE_INCLUDED

#include <memory>
#include <vk_mem_alloc.h>

#include "Render/ImageUtils.h"
#include "Render/Memory/AllocatedMemory.h"

namespace Engine {
    class RenderSystem;
    namespace RenderSystemState {
        class AllocatorState {
        public:
            enum class BufferType {
                // Staging buffer on host memory
                Staging,
                // Vertex buffer on device memory
                Vertex,
                // Uniform buffer on host memory
                Uniform
            };

        private:
            RenderSystem & m_system;
            VmaAllocator m_allocator {};

            static constexpr std::tuple<vk::BufferUsageFlags, VmaAllocationCreateFlags, VmaMemoryUsage> GetBufferFlags(BufferType type);

        public:
            AllocatorState(RenderSystem & system);

            AllocatorState(const AllocatorState &) = delete;
            AllocatorState(AllocatorState &&) = default;
            AllocatorState & operator = (const AllocatorState &) = delete;
            AllocatorState & operator = (AllocatorState &) = default;

            ~AllocatorState();

            void Create();
            VmaAllocator GetAllocator() const;

            AllocatedMemory AllocateBuffer(BufferType type, size_t size) const;
            std::unique_ptr <AllocatedMemory> AllocateBufferUnique(BufferType type, size_t size) const;

            AllocatedMemory AllocateImage(ImageUtils::ImageType type, VkExtent3D dimension, VkFormat format) const;
            std::unique_ptr <AllocatedMemory> AllocateImageUnique(ImageUtils::ImageType type, VkExtent3D dimension, VkFormat format) const;

            AllocatedMemory AllocateImageEx (
                ImageUtils::ImageType type, 
                VkExtent3D dimension, 
                VkFormat format, 
                uint32_t miplevel,
                uint32_t array_layers
            ) const;
        };
    }
}

#endif // RENDER_RENDERSYSTEM_ALLOCATORSTATE_INCLUDED
