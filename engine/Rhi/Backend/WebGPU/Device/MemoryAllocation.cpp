#include "Rhi/Device/MemoryAllocation.h"
#include "Rhi/Device/BindingRef.h"

#include "Rhi/Backend/WebGPU/WgpuHandles.h"

#include <SDL3/SDL.h>
#include <webgpu/webgpu.h>

namespace Engine::Rhi {
    /**
     * @brief The window this backend can keep open, and what it costs.
     *
     * The standard interface maps a buffer either at creation
     * (`WGPUBufferDescriptor::mappedAtCreation`) or through an asynchronous
     * `wgpuBufferMapAsync`; there is no synchronous map. A buffer the host writes is
     * therefore created mapped and stays mapped until it is destroyed - which is the
     * shape the mapped-window contract wants anyway: one address for the buffer's
     * life, and one unmap at the end.
     *
     * `FlushMemory` and `InvalidateMemory` do nothing here. The standard interface
     * has neither: a write becomes visible when the buffer is unmapped, and a buffer
     * that stays mapped for its life is coherent by construction.
     *
     * The identity follows the Vulkan side's decision: an allocation owns the value
     * `MintVulkanIdentity()` issued when it was created, and hands the same one back
     * for its whole life. The name is a leftover; the type is a `uint64_t` wrapper
     * that has nothing Vulkan-specific in it.
     */
    struct BufferAllocation::impl {
        WGPUBuffer buffer{nullptr};
        void *mapped{nullptr};
        BufferType type{};
        EpochTracker *retire_sink{nullptr};
        VulkanIdentity identity{MintVulkanIdentity()};
    };

    BufferAllocation::BufferAllocation(
        BufferHandle buffer, void *allocation, void *allocator, BufferType type, EpochTracker *retire_sink
    ) : VmaMemoryAllocation(allocation, allocator) {
        pimpl = std::make_unique<impl>();
        pimpl->buffer = ToWgpuBuffer(buffer);
        pimpl->type = type;
        pimpl->retire_sink = retire_sink;

        if (pimpl->buffer != nullptr && wgpuBufferGetMapState(pimpl->buffer) == WGPUBufferMapState_Mapped) {
            pimpl->mapped = wgpuBufferGetMappedRange(pimpl->buffer, 0, WGPU_WHOLE_MAP_SIZE);
        }
    }

    BufferAllocation::~BufferAllocation() {
        Destroy();
    }

    BufferAllocation::BufferAllocation(BufferAllocation &&other) noexcept = default;
    BufferAllocation &BufferAllocation::operator=(BufferAllocation &&other) noexcept = default;

    void BufferAllocation::Destroy() noexcept {
        if (!pimpl || pimpl->buffer == nullptr) {
            return;
        }
        // This backend has no retirement facility yet, so the buffer is released
        // here. The sink is kept so a future port of the retirement path has one
        // place to hook in rather than a search to do.
        if (pimpl->retire_sink == nullptr) {
            wgpuBufferRelease(pimpl->buffer);
        }
        pimpl->buffer = nullptr;
        pimpl->mapped = nullptr;
    }

    BufferHandle BufferAllocation::GetBuffer() const noexcept {
        return FromWgpuBuffer(pimpl->buffer);
    }

    VulkanIdentity BufferAllocation::GetIdentity() const noexcept {
        assert(pimpl);
        return pimpl->identity;
    }

    void BufferAllocation::ClearRetireSink() noexcept {
        if (pimpl) {
            pimpl->retire_sink = nullptr;
        }
    }

    std::byte *BufferAllocation::GetVMAddress() {
        if (pimpl->mapped == nullptr) {
            SDL_LogError(
                SDL_LOG_CATEGORY_RENDER,
                "WebGPU: GetVMAddress on a buffer that was not created mapped. The mapped-window "
                "contract requires WGPUBufferDescriptor::mappedAtCreation."
            );
            return nullptr;
        }
        return static_cast<std::byte *>(pimpl->mapped);
    }

    void BufferAllocation::FlushMemory(size_t, size_t) const {
        // No flush exists in the standard interface; remaining writes are published
        // by the unmap this backend performs when the allocation is destroyed.
    }

    void BufferAllocation::InvalidateMemory(size_t, size_t) const {
        // Nothing to invalidate: the mapping is coherent for the buffer's life.
    }

    BufferType BufferAllocation::GetMemoryType() const noexcept {
        assert(pimpl);
        return pimpl->type;
    }
} // namespace Engine::Rhi
