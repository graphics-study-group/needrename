#ifndef ENGINE_RHI_BACKEND_WEBGPU_WGPUHANDLES_INCLUDED
#define ENGINE_RHI_BACKEND_WEBGPU_WGPUHANDLES_INCLUDED

#include "Rhi/RhiTypes.h"
#include "Rhi/Texture/ImageUtils.h"

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

namespace Engine::Rhi {
    /**
     * @brief Map a seam image format to the standard interface's format.
     *
     * The two lists overlap almost completely for the formats the engine uses; the
     * ones that do not map return Undefined, which the caller reports rather than
     * creating a texture the device would reject.
     */
    inline WGPUTextureFormat ToWgpuTextureFormat(ImageFormat format) noexcept {
        switch (format) {
        case ImageFormat::R8G8B8A8UNorm:
            return WGPUTextureFormat_RGBA8Unorm;
        case ImageFormat::R8G8B8A8SRGB:
            return WGPUTextureFormat_RGBA8UnormSrgb;
        case ImageFormat::R8G8B8A8SNorm:
            return WGPUTextureFormat_RGBA8Snorm;
        case ImageFormat::R8G8B8A8UInt:
            return WGPUTextureFormat_RGBA8Uint;
        case ImageFormat::B8G8R8A8UNorm:
            return WGPUTextureFormat_BGRA8Unorm;
        case ImageFormat::B8G8R8A8SRGB:
            return WGPUTextureFormat_BGRA8UnormSrgb;
        case ImageFormat::R11G11B10UFloat:
            return WGPUTextureFormat_RG11B10Ufloat;
        case ImageFormat::R32G32B32A32SFloat:
            return WGPUTextureFormat_RGBA32Float;
        case ImageFormat::R32G32B32SFloat:
            // No RGB32Float in the standard list; a three-component 32-bit format is
            // not a render target on this interface. Callers that need it use RGBA.
            return WGPUTextureFormat_Undefined;
        case ImageFormat::R32G32SFloat:
            return WGPUTextureFormat_RG32Float;
        case ImageFormat::R32SFloat:
            return WGPUTextureFormat_R32Float;
        case ImageFormat::R32UInt:
            return WGPUTextureFormat_R32Uint;
        case ImageFormat::R16G16UInt:
            return WGPUTextureFormat_RG16Uint;
        case ImageFormat::D32SFLOAT:
            return WGPUTextureFormat_Depth32Float;
        case ImageFormat::UNDEFINED:
        default:
            return WGPUTextureFormat_Undefined;
        }
    }
} // namespace Engine::Rhi
namespace Engine::Rhi {
    inline SamplerHandle FromWgpuSampler(WGPUSampler sampler) noexcept {
        return FromWgpu<SamplerHandle>(sampler);
    }
    inline WGPUSampler ToWgpuSampler(SamplerHandle handle) noexcept {
        return ToWgpu<WGPUSampler>(handle);
    }
    inline ImageViewHandle FromWgpuTextureView(WGPUTextureView view) noexcept {
        return FromWgpu<ImageViewHandle>(view);
    }
    inline WGPUTextureView ToWgpuTextureView(ImageViewHandle handle) noexcept {
        return ToWgpu<WGPUTextureView>(handle);
    }
} // namespace Engine::Rhi
namespace Engine::Rhi {
    inline DescriptorSetLayoutHandle FromWgpuBindGroupLayout(WGPUBindGroupLayout layout) noexcept {
        return FromWgpu<DescriptorSetLayoutHandle>(layout);
    }
    inline WGPUBindGroupLayout ToWgpuBindGroupLayout(DescriptorSetLayoutHandle handle) noexcept {
        return ToWgpu<WGPUBindGroupLayout>(handle);
    }
} // namespace Engine::Rhi
namespace Engine::Rhi {
    inline DescriptorSetHandle FromWgpuBindGroup(WGPUBindGroup group) noexcept {
        return FromWgpu<DescriptorSetHandle>(group);
    }
    inline WGPUBindGroup ToWgpuBindGroup(DescriptorSetHandle handle) noexcept {
        return ToWgpu<WGPUBindGroup>(handle);
    }
} // namespace Engine::Rhi
namespace Engine::Rhi {
    inline ShaderModuleHandle FromWgpuShaderModule(WGPUShaderModule module) noexcept {
        return FromWgpu<ShaderModuleHandle>(module);
    }
    inline WGPUShaderModule ToWgpuShaderModule(ShaderModuleHandle handle) noexcept {
        return ToWgpu<WGPUShaderModule>(handle);
    }
} // namespace Engine::Rhi
namespace Engine::Rhi {
    inline CommandBufferHandle FromWgpuCommandEncoder(WGPUCommandEncoder encoder) noexcept {
        return FromWgpu<CommandBufferHandle>(encoder);
    }
    inline WGPUCommandEncoder ToWgpuCommandEncoder(CommandBufferHandle handle) noexcept {
        return ToWgpu<WGPUCommandEncoder>(handle);
    }
    inline PipelineHandle FromWgpuRenderPipeline(WGPURenderPipeline pipeline) noexcept {
        return FromWgpu<PipelineHandle>(pipeline);
    }
    inline WGPURenderPipeline ToWgpuRenderPipeline(PipelineHandle handle) noexcept {
        return ToWgpu<WGPURenderPipeline>(handle);
    }
    inline WGPUComputePipeline ToWgpuComputePipeline(PipelineHandle handle) noexcept {
        return reinterpret_cast<WGPUComputePipeline>(handle.GetValue());
    }
} // namespace Engine::Rhi
