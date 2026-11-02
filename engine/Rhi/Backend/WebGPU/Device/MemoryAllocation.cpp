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
        /// The host-side copy a mapped-window allocation writes into. A buffer that is
        /// bound in a shader cannot be mapped on this backend, so the address the caller
        /// writes through is this, and FlushMemory copies it to the device.
        std::vector<std::byte> shadow{};
        WGPUQueue queue{nullptr};
        /// Whether the buffer is a readback target, mapped on demand rather than at creation.
        bool readable{false};
    };

    BufferAllocation::BufferAllocation(
        BufferHandle buffer, void *allocation, void *allocator, BufferType type, EpochTracker *retire_sink
    ) : VmaMemoryAllocation(allocation, allocator) {
        pimpl = std::make_unique<impl>();
        pimpl->buffer = ToWgpuBuffer(buffer);
        pimpl->type = type;
        pimpl->retire_sink = retire_sink;

        // Whether the buffer is mapped is decided by the usage flags it was created with,
        // not by asking the device: wgpu-native implements wgpuBufferGetMapState as an
        // unimplemented!() that panics, which is how the first run of the device test
        // failed. A buffer created with mappedAtCreation is mapped; any other is not.
        const bool host_visible = type.Test(BufferTypeBits::HostRandomAccess)
                                  || type.Test(BufferTypeBits::HostSequentialAccess);
        if (pimpl->buffer != nullptr && host_visible) {
            // The three shapes an allocation can have, mirroring the usage flags the allocator
            // built: a readback target is mapped on demand, a host-written buffer that nothing
            // binds in a shader is mapped at creation, and a host-written buffer that is bound
            // in a shader gets a host copy published on flush.
            const bool readback = type.Test(BufferTypeBits::CopyTo) && !type.Test(BufferTypeBits::ShaderReadOnly)
                                  && !type.Test(BufferTypeBits::ShaderWrite)
                                  && !type.Test(BufferTypeBits::Index)
                                  && !type.Test(BufferTypeBits::Vertex)
                                  && !type.Test(BufferTypeBits::IndirectDrawCommand);
            const bool bound_in_shader = type.Test(BufferTypeBits::ShaderReadOnly)
                                         || type.Test(BufferTypeBits::ShaderWrite) || type.Test(BufferTypeBits::Index)
                                         || type.Test(BufferTypeBits::Vertex)
                                         || type.Test(BufferTypeBits::IndirectDrawCommand);
            if (readback) {
                pimpl->readable = true;
            } else if (bound_in_shader) {
                pimpl->shadow.resize(static_cast<size_t>(wgpuBufferGetSize(pimpl->buffer)));
                pimpl->queue = wgpuDeviceGetQueue(static_cast<WGPUDevice>(allocator));
            } else {
                pimpl->mapped = wgpuBufferGetMappedRange(pimpl->buffer, 0, WGPU_WHOLE_MAP_SIZE);
                if (pimpl->mapped == nullptr) {
                    pimpl->shadow.resize(static_cast<size_t>(wgpuBufferGetSize(pimpl->buffer)));
                    pimpl->queue = wgpuDeviceGetQueue(static_cast<WGPUDevice>(allocator));
                }
            }
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

    void BufferAllocation::PrepareReadback() {
        if (!pimpl->readable || pimpl->mapped != nullptr) {
            return;
        }
        // Mapping is asynchronous here, and this is the one place it can be awaited: the frame
        // pump calls it before it invokes a readback callback, which is outside the device's own
        // event dispatch - a map requested from inside that dispatch is rejected.
        bool done = false;
        bool mapped_ok = false;
        std::pair<bool *, bool *> state{&done, &mapped_ok};
        WGPUBufferMapCallbackInfo info{};
        info.mode = WGPUCallbackMode_AllowProcessEvents;
        info.callback = [](WGPUMapAsyncStatus status, WGPUStringView message, void *userdata1, void *) {
            auto *result = static_cast<std::pair<bool *, bool *> *>(userdata1);
            *result->first = true;
            *result->second = status == WGPUMapAsyncStatus_Success;
            if (status != WGPUMapAsyncStatus_Success) {
                SDL_LogError(
                    SDL_LOG_CATEGORY_RENDER,
                    "WebGPU: a readback buffer failed to map (status %d): %.*s",
                    static_cast<int>(status),
                    static_cast<int>(message.length),
                    message.data != nullptr ? message.data : ""
                );
            }
        };
        info.userdata1 = &state;
        wgpuBufferMapAsync(pimpl->buffer, WGPUMapMode_Read, 0, WGPU_WHOLE_MAP_SIZE, info);

        const WGPUInstance instance = RegisteredWgpuInstance();
        while (!done) {
            if (instance != nullptr) {
                wgpuInstanceProcessEvents(instance);
            }
#if defined(__EMSCRIPTEN__)
            emscripten_sleep(0);
#endif
        }
        if (mapped_ok) {
            pimpl->mapped = wgpuBufferGetMappedRange(pimpl->buffer, 0, WGPU_WHOLE_MAP_SIZE);
        }
    }

    std::byte *BufferAllocation::GetVMAddress() {
        if (pimpl->mapped != nullptr) {
            return static_cast<std::byte *>(pimpl->mapped);
        }
        if (!pimpl->shadow.empty()) {
            // The buffer is bound in a shader, so the address is a host copy that
            // `FlushMemory` publishes. That is the mapped window on this backend.
            return pimpl->shadow.data();
        }
        if (pimpl->readable) {
            // A readback target: map now, which dispatches the device's events until the map
            // completes. The caller reaches this after the copy by way of the completion
            // callback, so the wait is short.
            bool done = false;
            bool mapped_ok = false;
            std::pair<bool *, bool *> state{&done, &mapped_ok};
            WGPUBufferMapCallbackInfo info{};
            info.mode = WGPUCallbackMode_AllowProcessEvents;
            info.callback = [](WGPUMapAsyncStatus status, WGPUStringView message, void *userdata1, void *) {
                auto *result = static_cast<std::pair<bool *, bool *> *>(userdata1);
                *result->first = true;
                *result->second = status == WGPUMapAsyncStatus_Success;
                if (status != WGPUMapAsyncStatus_Success) {
                    SDL_LogError(
                        SDL_LOG_CATEGORY_RENDER,
                        "WebGPU: a readback buffer failed to map (status %d): %.*s",
                        static_cast<int>(status),
                        static_cast<int>(message.length),
                        message.data != nullptr ? message.data : ""
                    );
                }
            };
            info.userdata1 = &state;
            wgpuBufferMapAsync(pimpl->buffer, WGPUMapMode_Read, 0, WGPU_WHOLE_MAP_SIZE, info);

            const WGPUInstance instance = RegisteredWgpuInstance();
            while (!done) {
                if (instance != nullptr) {
                    wgpuInstanceProcessEvents(instance);
                }
#if defined(__EMSCRIPTEN__)
                emscripten_sleep(0);
#endif
            }
            if (!mapped_ok) {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "WebGPU: mapping a readback buffer failed.");
                return nullptr;
            }
            pimpl->mapped = wgpuBufferGetMappedRange(pimpl->buffer, 0, WGPU_WHOLE_MAP_SIZE);
            return static_cast<std::byte *>(pimpl->mapped);
        }
        SDL_LogError(
            SDL_LOG_CATEGORY_RENDER,
            "WebGPU: GetVMAddress on a buffer that has no host mapping."
        );
        return nullptr;
    }

    void BufferAllocation::FlushMemory(size_t, size_t) const {
        if (pimpl->mapped != nullptr || pimpl->shadow.empty() || pimpl->queue == nullptr) {
            return;
        }
        wgpuQueueWriteBuffer(pimpl->queue, pimpl->buffer, 0, pimpl->shadow.data(), pimpl->shadow.size());
    }

    void BufferAllocation::InvalidateMemory(size_t, size_t) const {
        // Nothing to invalidate: the mapping is coherent for the buffer's life.
    }

    BufferType BufferAllocation::GetMemoryType() const noexcept {
        assert(pimpl);
        return pimpl->type;
    }
} // namespace Engine::Rhi

namespace Engine::Rhi {
    /**
     * @brief An image this backend created, and its identity.
     *
     * The standard interface has no separate device memory for an image: a
     * WGPUTexture is the image. `allocation` and `allocator` therefore carry the
     * texture and the device, and destruction releases the texture.
     */
    struct ImageAllocation::impl {
        ImageHandle image{};
        ImageMemoryType type{};
        VulkanIdentity identity{MintVulkanIdentity()};
    };

    ImageAllocation::ImageAllocation(ImageHandle image, void *allocation, void *allocator, ImageMemoryType type) :
        VmaMemoryAllocation(allocation, allocator), pimpl(std::make_unique<impl>()) {
        pimpl->image = image;
        pimpl->type = type;
    }

    ImageAllocation::~ImageAllocation() {
        Destory();
    }

    ImageAllocation::ImageAllocation(ImageAllocation &&other) noexcept = default;
    ImageAllocation &ImageAllocation::operator=(ImageAllocation &&other) noexcept = default;

    void ImageAllocation::Destory() noexcept {
        if (!pimpl || !pimpl->image.IsValid()) {
            return;
        }
        wgpuTextureRelease(ToWgpuTexture(pimpl->image));
        pimpl->image = {};
    }

    ImageHandle ImageAllocation::GetImage() const noexcept {
        assert(pimpl);
        return pimpl->image;
    }

    ImageMemoryType ImageAllocation::GetMemoryType() const noexcept {
        assert(pimpl);
        return pimpl->type;
    }

    VulkanIdentity ImageAllocation::GetIdentity() const noexcept {
        assert(pimpl);
        return pimpl->identity;
    }
} // namespace Engine::Rhi

