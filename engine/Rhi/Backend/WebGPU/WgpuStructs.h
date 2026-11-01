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
} // namespace Engine::Rhi

#endif // ENGINE_RHI_BACKEND_WEBGPU_WGPUSTRUCTS_INCLUDED

