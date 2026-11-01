#include "Rhi/Device/AllocatorState.h"

#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/Device/DeviceInterface.h"
#include "Rhi/Device/MemoryAllocation.h"
#include "Rhi/Submission/EpochTracker.h"

#include <SDL3/SDL.h>
#include <webgpu/webgpu.h>

namespace Engine::Rhi {
    namespace {
        /**
         * @brief Turn the seam's buffer usage into the standard interface's flags.
         *
         * The two vocabularies are close enough that this is a table, not a
         * translation: the seam names what a buffer is *for*, and the standard
         * interface names what operations are legal on it.
         *
         * A host-visible buffer is the one case that needs more than a flag. The
         * standard interface has no coherent mapping that is written without being
         * mapped, so `HostRandomAccess` and `HostSequentialAccess` add both map
         * flags and the caller creates the buffer mapped at creation.
         */
        WGPUBufferUsage ToWgpuUsage(BufferType type) {
            WGPUBufferUsage usage = WGPUBufferUsage_None;
            if (type.Test(BufferTypeBits::CopyFrom)) {
                usage |= WGPUBufferUsage_CopySrc;
            }
            if (type.Test(BufferTypeBits::CopyTo)) {
                usage |= WGPUBufferUsage_CopyDst;
            }
            if (type.Test(BufferTypeBits::ShaderReadOnly)) {
                usage |= WGPUBufferUsage_Uniform;
            }
            if (type.Test(BufferTypeBits::ShaderWrite)) {
                usage |= WGPUBufferUsage_Storage;
            }
            if (type.Test(BufferTypeBits::Index)) {
                usage |= WGPUBufferUsage_Index;
            }
            if (type.Test(BufferTypeBits::Vertex)) {
                usage |= WGPUBufferUsage_Vertex;
            }
            if (type.Test(BufferTypeBits::IndirectDrawCommand)) {
                usage |= WGPUBufferUsage_Indirect;
            }
            if (type.Test(BufferTypeBits::HostRandomAccess) || type.Test(BufferTypeBits::HostSequentialAccess)) {
                usage |= WGPUBufferUsage_MapRead | WGPUBufferUsage_MapWrite;
                // A mapped write is still a write: the device reads it, so the
                // buffer has to be a legal copy destination too.
                usage |= WGPUBufferUsage_CopyDst;
            }
            return usage;
        }

        bool HostVisible(BufferType type) {
            return type.Test(BufferTypeBits::HostRandomAccess) || type.Test(BufferTypeBits::HostSequentialAccess);
        }
    } // namespace

    struct AllocatorState::impl {
        WGPUDevice device{nullptr};
        EpochTracker *retire_sink{nullptr};
    };

    AllocatorState::AllocatorState(DeviceInterface &device_interface) :
        m_device_interface(&device_interface), pimpl(std::make_unique<impl>()) {
        pimpl->device = ToWgpuDevice(device_interface.GetDevice());
    }

    AllocatorState::AllocatorState(AllocatorState &&) noexcept = default;
    AllocatorState::~AllocatorState() = default;

    void AllocatorState::SetRetireSink(EpochTracker *sink) noexcept {
        pimpl->retire_sink = sink;
    }

    BufferAllocation AllocatorState::AllocateBuffer(
        BufferType type, size_t size, const std::string &name
    ) const {
        WGPUBufferDescriptor desc{};
        desc.size = size;
        desc.usage = ToWgpuUsage(type);
        // A host-writable buffer is mapped from the moment it exists. There is no
        // synchronous map afterwards, and the mapped-window contract wants one
        // address for the buffer's whole life anyway.
        desc.mappedAtCreation = HostVisible(type) ? WGPU_TRUE : WGPU_FALSE;
        desc.label = WGPUStringView{name.data(), name.size()};

        WGPUBuffer buffer = wgpuDeviceCreateBuffer(pimpl->device, &desc);
        if (buffer == nullptr) {
            // The standard interface reports failures through the uncaptured-error
            // callback rather than a status code; a null buffer here means it did.
            SDL_LogError(
                SDL_LOG_CATEGORY_RENDER,
                "WebGPU: buffer creation failed (%zu bytes, usage 0x%x).",
                size,
                static_cast<unsigned>(desc.usage)
            );
        }

        return BufferAllocation(
            FromWgpuBuffer(buffer), buffer, pimpl->device, type, pimpl->retire_sink
        );
    }

    std::unique_ptr<BufferAllocation> AllocatorState::AllocateBufferUnique(
        BufferType type, size_t size, const std::string &name
    ) const noexcept {
        // A unique allocation is built from the value form; the move is what makes
        // it unique.
        return std::make_unique<BufferAllocation>(AllocateBuffer(type, size, name));
    }

    size_t AllocatorState::GetLiveAllocationCount() const noexcept {
        // No allocation ledger on this backend: a buffer is one object with no
        // separate device memory to account for. Reporting zero rather than a
        // guess keeps a caller from drawing conclusions from a made-up number.
        return 0;
    }
} // namespace Engine::Rhi

