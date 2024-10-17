#ifndef RENDER_RENDERSYSTEM_ALLOCATORSTATE_INCLUDED
#define RENDER_RENDERSYSTEM_ALLOCATORSTATE_INCLUDED

#include <memory>
#include <vk_mem_alloc.h>

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
            std::weak_ptr <RenderSystem> m_system {};
            VmaAllocator m_allocator {};

            static std::tuple<vk::BufferUsageFlags, VmaAllocationCreateFlags, VmaMemoryUsage> GetBufferFlags(BufferType type);
            static void RaiseException(VkResult result);

        public:
            AllocatorState() = default;
            AllocatorState(const AllocatorState &) = delete;
            void operator = (const AllocatorState &) = delete;

            ~AllocatorState();

            void Create(std::shared_ptr <RenderSystem> system);
            VmaAllocator GetAllocator() const;

            AllocatedMemory AllocateBuffer(BufferType type, size_t size) const;
            std::unique_ptr <AllocatedMemory> AllocateBufferUnique(BufferType type, size_t size) const;
        };
    }
}

#endif // RENDER_RENDERSYSTEM_ALLOCATORSTATE_INCLUDED
