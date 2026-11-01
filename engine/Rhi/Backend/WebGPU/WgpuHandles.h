#ifndef ENGINE_RHI_BACKEND_WEBGPU_WGPUHANDLES_INCLUDED
#define ENGINE_RHI_BACKEND_WEBGPU_WGPUHANDLES_INCLUDED

#include "Rhi/RhiTypes.h"

#include <webgpu/webgpu.h>

namespace Engine::Rhi {
    // The seam's handles are opaque integers. A backend decides what they mean, and
    // for this one they are the pointers the standard interface hands out. Nothing
    // outside this backend interprets them.

    template <typename Handle, typename WgpuObject>
    inline Handle FromWgpu(WgpuObject object) noexcept {
        return Handle::FromValue(reinterpret_cast<uintptr_t>(object));
    }

    template <typename WgpuObject, typename Handle>
    inline WgpuObject ToWgpu(Handle handle) noexcept {
        return reinterpret_cast<WgpuObject>(handle.GetValue());
    }

    inline DeviceHandle FromWgpuDevice(WGPUDevice device) noexcept {
        return FromWgpu<DeviceHandle>(device);
    }
    inline WGPUDevice ToWgpuDevice(DeviceHandle handle) noexcept {
        return ToWgpu<WGPUDevice>(handle);
    }

    inline InstanceHandle FromWgpuInstance(WGPUInstance instance) noexcept {
        return FromWgpu<InstanceHandle>(instance);
    }
    inline WGPUInstance ToWgpuInstance(InstanceHandle handle) noexcept {
        return ToWgpu<WGPUInstance>(handle);
    }

    inline PhysicalDeviceHandle FromWgpuAdapter(WGPUAdapter adapter) noexcept {
        return FromWgpu<PhysicalDeviceHandle>(adapter);
    }
    inline WGPUAdapter ToWgpuAdapter(PhysicalDeviceHandle handle) noexcept {
        return ToWgpu<WGPUAdapter>(handle);
    }

    inline BufferHandle FromWgpuBuffer(WGPUBuffer buffer) noexcept {
        return FromWgpu<BufferHandle>(buffer);
    }
    inline WGPUBuffer ToWgpuBuffer(BufferHandle handle) noexcept {
        return ToWgpu<WGPUBuffer>(handle);
    }

    inline ImageHandle FromWgpuTexture(WGPUTexture texture) noexcept {
        return FromWgpu<ImageHandle>(texture);
    }
    inline WGPUTexture ToWgpuTexture(ImageHandle handle) noexcept {
        return ToWgpu<WGPUTexture>(handle);
    }
} // namespace Engine::Rhi

#endif // ENGINE_RHI_BACKEND_WEBGPU_WGPUHANDLES_INCLUDED



