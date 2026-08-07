#ifndef GPU_CONTEXT_GPUCONTEXT_INCLUDED
#define GPU_CONTEXT_GPUCONTEXT_INCLUDED

#include "AllocatorState.h"
#include "DeviceInterface.h"
#include "gpu_context_export.h"

#include <memory>
#include <vulkan/vulkan.hpp>

namespace Engine {
    namespace RenderSystemState {
        struct DeviceConfiguration;
    }

    /**
     * @brief Lightweight aggregation of DeviceInterface and AllocatorState.
     *
     * Provides Vulkan device management and memory allocation
     * without any window, surface, or swapchain dependency.
     * Used by both RenderSystem (windowed) and standalone headless workloads.
     */
    class GPU_CONTEXT_API GpuContext {
        class impl;
        std::unique_ptr<impl> pimpl;

    public:
        GpuContext(RenderSystemState::DeviceInterface::DeviceConfiguration cfg);
        ~GpuContext();

        GpuContext(const GpuContext &) = delete;
        GpuContext &operator=(const GpuContext &) = delete;
        GpuContext(GpuContext &&) = default;

        vk::Device GetDevice() const;
        const RenderSystemState::DeviceInterface &GetDeviceInterface() const;
        const RenderSystemState::AllocatorState &GetAllocatorState() const;
    };
} // namespace Engine

#endif
