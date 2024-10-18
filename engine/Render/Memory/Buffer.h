#ifndef RENDER_PIPELINE_MEMORY_BUFFER_INCLUDED
#define RENDER_PIPELINE_MEMORY_BUFFER_INCLUDED

#include <vulkan/vulkan.hpp>
#include <memory>

#include "Render/RenderSystem/AllocatorState.h"

namespace Engine{

    class RenderSystem;

    /// @brief A buffer with allocated memory, which could be on device or host.
    /// TODO: Rewrite with a Vulkan allocator library
    class Buffer {
    public:

        using BufferType = RenderSystemState::AllocatorState::BufferType;
        Buffer (std::weak_ptr <RenderSystem> system);

        /// @brief Create a buffer, and perform allocation if needed.
        /// @param type 
        /// @param size 
        void Create(BufferType type, size_t size);

        vk::Buffer GetBuffer () const;

        size_t GetSize() const;

        std::byte * Map() const;
        void Unmap() const;
    
    protected:
        size_t m_size {0ULL};

        std::weak_ptr <RenderSystem> m_system;
        std::unique_ptr <AllocatedMemory> m_allocated_memory {};
    };
}

#endif // RENDER_PIPELINE_MEMORY_BUFFER_INCLUDED
