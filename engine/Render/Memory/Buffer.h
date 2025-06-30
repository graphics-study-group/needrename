#ifndef RENDER_MEMORY_BUFFER_INCLUDED
#define RENDER_MEMORY_BUFFER_INCLUDED

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
        Buffer (std::shared_ptr <RenderSystem> system);
        Buffer (RenderSystem & system);

        /// @brief Create a buffer, and perform allocation if needed.
        /// @param type 
        /// @param size 
        void Create(BufferType type, size_t size, const std::string & name = "");

        vk::Buffer GetBuffer () const;

        size_t GetSize() const;

        /**
         * @brief Map the buffer memory to a host pointer.
         * 
         * The pointer is automatically unmapped on deconstruction.
         * You don't need to match `Unmap()` manually before cleaning up.
         */
        std::byte * Map() const;

        /**
         * @brief Flush the memory write to be visible on device.
         * 
         * Generally you don't need to manually call this member, as memories that
         * need to be flushed are usually coherent.
         * 
         * @param offset Offset of the region to be flushed
         * @param size Size of the region to be flushed, or whole region if 0.
         */
        void Flush(size_t offset = 0, size_t size = 0) const;

        /**
         * @brief Invalidate the memory so that device write are visible on host.
         * 
         * Generally you don't need to manually call this member, as memories that
         * need to be invalidated are usually coherent.
         * 
         * @param offset Offset of the region to be invalidated
         * @param size Size of the region to be invalidated, or whole region if 0.
         */
        void Invalidate (size_t offset = 0, size_t size = 0) const;

        /**
         * @brief Unmap the host pointer.
         * 
         * Generally you don't need to manually call this member, as clean up is
         * automatically done by the underlying `AllocatedMemory`.
         */
        void Unmap() const;
    
    protected:
        size_t m_size {0ULL};

        RenderSystem & m_system;
        std::unique_ptr <AllocatedMemory> m_allocated_memory {};
    };
}

#endif // RENDER_MEMORY_BUFFER_INCLUDED
