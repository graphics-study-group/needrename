#ifndef RENDER_MEMORY_MEMORYALLOCATION_INCLUDED
#define RENDER_MEMORY_MEMORYALLOCATION_INCLUDED

#include "MemoryTypes.h"

class VmaAllocation_T;
class VmaAllocator_T;
class VmaAllocationInfo;
typedef VmaAllocation_T *VmaAllocation;
typedef VmaAllocator_T *VmaAllocator;

namespace vk {
    class Image;
    class Buffer;
} // namespace vk

namespace Engine {
    /**
     * @brief A piece of memory allocated by Vulkan via Vulkan Memory Allocator.
     * 
     * Memory allocation is a complex topic in Vulkan programming. In short,
     * this class represents a typeless memory allocation in Vulkan, on device
     * or on host, over which an image or a buffer can be created on.
     * 
     * This class should not be visible to users. Use `ImageAllocation` for
     * images and `BufferAllocation` for buffers.
     * 
     * Use `Engine::RenderSystemState::AllocatorState` to allocate memory.
     * 
     * @invariant This class, once created, is guaranteed to hold a vaild memory
     * allocation until moved or destructed.
     */
    class VmaMemoryAllocation {
        VmaAllocation m_allocation;
        VmaAllocator m_allocator;

    public:
        VmaMemoryAllocation() : m_allocation(nullptr), m_allocator(nullptr) {}

        /// @brief Create an allocation, called from `Engine::RenderSystemState::AllocatorState`.
        VmaMemoryAllocation(
            VmaAllocation allocation,
            VmaAllocator allocator
        ) : m_allocation(allocation), m_allocator(allocator) {}
        ~VmaMemoryAllocation() {}

        VmaMemoryAllocation(const VmaMemoryAllocation &) = delete;
        VmaMemoryAllocation &operator=(const VmaMemoryAllocation &) = delete;

        /// @brief Construct an allocation from another allocation.
        VmaMemoryAllocation(VmaMemoryAllocation &&other) = default;
        /// @brief Construct an allocation from another allocation.
        VmaMemoryAllocation &operator=(VmaMemoryAllocation &&other) = default;

        const VmaAllocation &GetAllocation() const noexcept {
            assert(m_allocation);
            return m_allocation;
        }
        const VmaAllocator &GetAllocator() const noexcept {
            assert(m_allocator);
            return m_allocator;
        }
    };

    /**
     * @brief A piece of memory allocation for images.
     * 
     * Use `Engine::RenderSystemState::AllocatorState` to perform memory
     * allocation.
     * 
     * @invariant This class, once created, is guaranteed to hold a vaild image
     * allocation until moved or destructed.
     */
    class ImageAllocation : private VmaMemoryAllocation {
        struct impl;
        std::unique_ptr<impl> pimpl;

        /// @brief Destory the resource stored inside the pimpl pointer, and reset the pointer.
        void Destory() noexcept;

    public:
        /// @brief Create an image allocation, called from `Engine::RenderSystemState::AllocatorState`.
        ImageAllocation(vk::Image image, VmaAllocation allocation, VmaAllocator allocator, ImageMemoryType type);

        ~ImageAllocation();

        ImageAllocation(const ImageAllocation &) = delete;
        void operator=(const ImageAllocation &) = delete;

        /**
         * @brief Construct an allocation from another allocation.
         * 
         * The other allocation will be invaild after moving. Calling any method
         * invokes undefined behavior.
         */
        ImageAllocation(ImageAllocation &&other) noexcept;

        /**
         * @brief Acquire an allocation from another allocation.
         * 
         * The other allocation will be invaild after moving. Calling any method
         * invokes undefined behavior.
         */
        ImageAllocation &operator=(ImageAllocation &&other) noexcept;

        /// @brief Get the underlying Vulkan image object.
        vk::Image GetImage() const noexcept;
        /// @brief Query the memory type specified on creation.
        ImageMemoryType GetMemoryType() const noexcept;
    };

    /**
     * @brief A piece of memory allocation for buffer.
     * 
     * This class represents a low-level memory interface for buffer memory.
     * Users should prefer to use `Engine::DeviceBuffer` or related classes
     * to manipulate data.
     * 
     * Use `Engine::RenderSystemState::AllocatorState` to perform memory
     * allocation.
     * 
     * @invariant This class, once created, is guaranteed to hold a vaild buffer
     * allocation until moved or destructed.
     */
    class BufferAllocation : private VmaMemoryAllocation {
        struct impl;
        std::unique_ptr<impl> pimpl;

        /// @brief Destory the resource stored inside the pimpl pointer, and reset the pointer.
        void Destroy() noexcept;

    public:
        /// @brief Create a buffer allocation, called from `Engine::RenderSystemState::AllocatorState`.
        BufferAllocation(vk::Buffer buffer, VmaAllocation allocation, VmaAllocator allocator, BufferType type);
        ~BufferAllocation();

        BufferAllocation(const BufferAllocation &) = delete;
        BufferAllocation &operator=(const BufferAllocation &) = delete;

        /**
         * @brief Construct an allocation from another allocation.
         * 
         * The other allocation will be invaild after moving. Calling any method
         * invokes undefined behavior.
         */
        BufferAllocation(BufferAllocation &&other) noexcept;

        /**
         * @brief Acquire an allocation from another allocation.
         * 
         * The other allocation will be invaild after moving. Calling any method
         * invokes undefined behavior.
         */
        BufferAllocation &operator=(BufferAllocation &&other) noexcept;

        /// @brief Get the underlying Vulkan buffer object.
        vk::Buffer GetBuffer() const noexcept;

        /**
         * @brief Get the memory address on the host virtual memory
         * that maps to the buffer.
         *
         * @exception Throws if the buffer cannot be mapped to host VM.
         * @return pointer to the content of the buffer
         */
        std::byte *GetVMAddress();

        /**
         * @brief Flush the memory so that host writes are visible on the device
         *
         * @exception std::runtime_error Rethrows all exception if the underlying Vulkan call fails
         */
        void FlushMemory(size_t offset = 0, size_t size = 0) const;

        /**
         * @brief Invalidate the memory so that device writes are visible on
         * the host.
         *
         * @exception std::runtime_error Rethrows all exception if the underlying Vulkan call fails
         */
        void InvalidateMemory(size_t offset = 0, size_t size = 0) const;

        /// @brief Query the memory type specified on creation.
        BufferType GetMemoryType() const noexcept;
    };
} // namespace Engine

#endif // RENDER_MEMORY_MEMORYALLOCATION_INCLUDED
