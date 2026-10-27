#ifndef RENDER_ASSET_SHADER_SHADERKERNEL_INCLUDED
#define RENDER_ASSET_SHADER_SHADERKERNEL_INCLUDED

#include <Render/Asset/Shader/ShaderAsset.h>

#include <Rhi/Device/DeviceContext.h>
#include <Rhi/Pipeline/ComputeKernel.h>

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
     */
    inline Rhi::ComputeKernel &RequestComputeKernel(Rhi::DeviceContext &device_context, const ShaderAsset &shader) {
        return device_context.RequestComputeKernel(shader.GetGUID().string(), shader.binary, shader.m_name);
    }
} // namespace Engine

#endif // RENDER_ASSET_SHADER_SHADERKERNEL_INCLUDED
