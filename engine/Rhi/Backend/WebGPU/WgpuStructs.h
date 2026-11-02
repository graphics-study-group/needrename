#ifndef ENGINE_RHI_BACKEND_WEBGPU_WGPUSTRUCTS_INCLUDED
#define ENGINE_RHI_BACKEND_WEBGPU_WGPUSTRUCTS_INCLUDED

#include <webgpu/webgpu.h>

namespace Engine::Rhi {
    /**
     * @brief What this backend calls its queues.
     *
     * Vulkan has queue families, several queues per family and a separate present
     * queue; the standard WebGPU interface has one device queue and presentation
     * happens through a surface configuration. The header that the rest of the
     * engine sees only forward-declares this type, so each backend defines the
     * fields that make sense for it.
     */
    struct QueueInfo {
        /// @brief The device's queue. There is exactly one.
        WGPUQueue graphicsQueue{nullptr};
    };

    /**
     * @brief What this backend can say about a swap chain.
     *
     * The standard interface configures a surface rather than enumerating its
     * formats, so there is nothing to enumerate here. The type exists because the
     * shared header names it; a caller that branches on backend capability uses
     * `DeviceCapabilities` instead.
     */
    struct SwapchainSupport {
        /// @brief Always false: this backend does not expose surface formats.
        bool supported{false};
    };

    /// @brief The bind-group index the constant block lives at.
    ///
    /// A shader's push-constant block becomes a uniform buffer, because neither this
    /// interface nor WGSL has push constants. The bake puts it in group 3 - the last of the
    /// four groups the interface guarantees - and every pipeline layout that declares a
    /// constant range carries a layout for that group, so one encoder-owned ring serves them
    /// all.
    constexpr uint32_t k_constant_bind_group = 3;

    /// @brief The layout entry of the constant block: one dynamic uniform buffer.
    ///
    /// `hasDynamicOffset` is what lets one buffer hold a window per draw: the offset is chosen
    /// when the group is bound, not when it is created.
    inline WGPUBindGroupLayoutEntry ConstantBlockLayoutEntry() noexcept {
        WGPUBindGroupLayoutEntry entry{};
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment | WGPUShaderStage_Compute;
        entry.buffer.type = WGPUBufferBindingType_Uniform;
        entry.buffer.hasDynamicOffset = true;
        // Zero is the smallest minimum the interface allows and is valid: a binding is checked
        // against the range it actually covers when the shader reads it.
        entry.buffer.minBindingSize = 0;
        return entry;
    }

    /// @brief The byte alignment a dynamic uniform offset must have.
    constexpr size_t k_constant_window = 256;
} // namespace Engine::Rhi

#endif // ENGINE_RHI_BACKEND_WEBGPU_WGPUSTRUCTS_INCLUDED

