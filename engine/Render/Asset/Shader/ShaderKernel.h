#ifndef RENDER_ASSET_SHADER_SHADERKERNEL_INCLUDED
#define RENDER_ASSET_SHADER_SHADERKERNEL_INCLUDED

#include <Render/Asset/Shader/ShaderAsset.h>

#include <Rhi/Device/DeviceContext.h>
#include <Rhi/Pipeline/ComputeKernel.h>
#include <Rhi/Pipeline/ShaderInterfaceDescription.h>
#include <Rhi/Pipeline/ShaderModule.h>

#include <stdexcept>

namespace Engine {
    /**
     * @brief Get the compute kernel for a shader asset.
     *
     * The asset's GUID is the kernel's module identity, so every requester of
     * one asset resolves to the same kernel, and the asset's name is its debug
     * name. A request that hits neither reads the binary nor scans it.
     *
     * The asset is expected to carry a GUID, which every asset does from
     * construction; this wrapper does not inspect the identity's form.
     *
     * The two backends need different forms of the same asset, which the bake
     * stored side by side: the Vulkan backend reflects the SPIR-V words, and a
     * backend without a reflection library reads the WGSL and the interface
     * description that were baked next to them.
     */
    inline Rhi::ComputeKernel &RequestComputeKernel(Rhi::DeviceContext &device_context, const ShaderAsset &shader) {
#if defined(ENGINE_RHI_BACKEND_WEBGPU)
        if (shader.wgsl.empty()) {
            throw std::runtime_error(
                "Shader asset '" + shader.m_name
                + "' has no WGSL variant: a WebGPU build stages its built-in shaders in baked form."
            );
        }
        const std::optional<Rhi::ShaderInterfaceDescription> description =
            Rhi::ShaderInterfaceDescription::FromString(shader.interface_description);
        if (!description.has_value()) {
            throw std::runtime_error(
                "Shader asset '" + shader.m_name + "' has no readable interface description."
            );
        }
        const Rhi::ShaderModule module = Rhi::ShaderModule::Create(
            device_context.GetDeviceInterface(),
            std::as_bytes(std::span{shader.wgsl.data(), shader.wgsl.size()}),
            shader.m_name
        );
        return device_context.RequestComputeKernelFromModule(
            shader.GetGUID().string(), module.GetHandle(), description.value(), description->entry_point,
            shader.m_name
        );
#else
        return device_context.RequestComputeKernel(shader.GetGUID().string(), shader.binary, shader.m_name);
#endif
    }
} // namespace Engine

#endif // RENDER_ASSET_SHADER_SHADERKERNEL_INCLUDED
