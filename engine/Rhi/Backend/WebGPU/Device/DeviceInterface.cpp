#include "Rhi/Device/DeviceInterface.h"

#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/Backend/WebGPU/WgpuStructs.h"
#include "Rhi/Backend/WebGPU/WgpuNativeLog.h"

#include <SDL3/SDL.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <cstdio>
#include <thread>
#include <webgpu/webgpu.h>

namespace Engine::Rhi {
    namespace {
        /// @brief What a request callback stores its result in.
        ///
        /// The standard interface delivers adapters and devices asynchronously. A
        /// blocking constructor has to pump the instance until the callback has
        /// run, which is what `PumpUntil` does - and it is the same shape the
        /// backend's frame loop uses, so the wait path is exercised from day one.
        template <typename Handle>
        struct Request {
            Handle handle{nullptr};
            std::atomic<bool> done{false};
        };

        void OnAdapterRequested(
            WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView, void *userdata1, void *
        ) {
            auto *request = static_cast<Request<WGPUAdapter> *>(userdata1);
            if (status == WGPURequestAdapterStatus_Success) {
                request->handle = adapter;
            } else {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "WebGPU: requestAdapter did not succeed.");
            }
            request->done.store(true);
        }

        void OnDeviceRequested(
            WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView, void *userdata1, void *
        ) {
            auto *request = static_cast<Request<WGPUDevice> *>(userdata1);
            if (status == WGPURequestDeviceStatus_Success) {
                request->handle = device;
            } else {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "WebGPU: requestDevice did not succeed.");
            }
            request->done.store(true);
        }

        void OnUncapturedError(WGPUDevice const *, WGPUErrorType type, WGPUStringView message, void *, void *) {
            SDL_LogError(
                SDL_LOG_CATEGORY_RENDER,
                "WebGPU uncaptured error (type %d): %.*s",
                static_cast<int>(type),
                static_cast<int>(message.length),
                message.data != nullptr ? message.data : ""
            );
        }

        /// @brief Drive the instance until `done`, without blocking on the device.
        void PumpUntil(WGPUInstance instance, const std::atomic<bool> &done, const char *what) {
            // The callback mode is process-events, so work happens while events are
            // processed. A short sleep keeps this from spinning a core white the
            // request is in flight.
            while (!done.load()) {
                wgpuInstanceProcessEvents(instance);
                // A sleep that blocks the page keeps the callback from ever running: the browser
                // answers this request on the event loop the caller is occupying. emscripten_sleep
                // is the one that yields to it, and Asyncify is what turns it into a pause.
#if defined(__EMSCRIPTEN__)
                emscripten_sleep(1);
#else
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
            }
            static_cast<void>(what);
        }
    } // namespace

    struct DeviceInterface::impl {
        WGPUInstance instance{nullptr};
        WGPUAdapter adapter{nullptr};
        WGPUDevice device{nullptr};
        QueueInfo queue_info{};
        WGPULimits limits{};
    };

    DeviceInterface::DeviceInterface(DeviceConfiguration cfg) : pimpl(std::make_unique<impl>()) {
        // Before anything asks the device to do work: the implementation's own log is where a
        // status of "Validation Error" gets its reason.
        ConfigureWgpuLoggingFromEnvironment();

        WGPUInstanceDescriptor instance_desc{};
        // wgpu-native reads the backend selection from the environment; the browser
        // ignores this field entirely.
        pimpl->instance = wgpuCreateInstance(&instance_desc);
        if (pimpl->instance == nullptr) {
            throw std::runtime_error("WebGPU: instance creation failed.");
        }

        Request<WGPUAdapter> adapter_request{};
        WGPURequestAdapterOptions adapter_options{};
        adapter_options.powerPreference = WGPUPowerPreference_HighPerformance;
        WGPURequestAdapterCallbackInfo adapter_callback{};
        adapter_callback.mode = WGPUCallbackMode_AllowProcessEvents;
        adapter_callback.callback = OnAdapterRequested;
        adapter_callback.userdata1 = &adapter_request;
        wgpuInstanceRequestAdapter(pimpl->instance, &adapter_options, adapter_callback);
        PumpUntil(pimpl->instance, adapter_request.done, "requestAdapter");
        pimpl->adapter = adapter_request.handle;
        if (pimpl->adapter == nullptr) {
            throw std::runtime_error("WebGPU: no adapter available.");
        }

        // What this device has to be able to do, above the defaults. The engine's own physics
        // kernel binds eleven storage buffers in one group, which is past the default of eight,
        // so the adapter's own maximum is requested. A limit the adapter does not offer cannot be
        // asked for, so this reads what it has rather than assuming.
        WGPULimits adapter_limits{};
        if (!wgpuAdapterGetLimits(pimpl->adapter, &adapter_limits)) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "WebGPU: the adapter did not report its limits.");
        }
        WGPULimits requested_limits = WGPU_LIMITS_INIT;
        requested_limits.maxStorageBuffersPerShaderStage = adapter_limits.maxStorageBuffersPerShaderStage;
        requested_limits.maxStorageTexturesPerShaderStage = adapter_limits.maxStorageTexturesPerShaderStage;
        requested_limits.maxBindGroups = adapter_limits.maxBindGroups;

        Request<WGPUDevice> device_request{};
        WGPUDeviceDescriptor device_desc{};
        device_desc.requiredLimits = &requested_limits;
        device_desc.uncapturedErrorCallbackInfo.callback = OnUncapturedError;
        WGPURequestDeviceCallbackInfo device_callback{};
        device_callback.mode = WGPUCallbackMode_AllowProcessEvents;
        device_callback.callback = OnDeviceRequested;
        device_callback.userdata1 = &device_request;
        wgpuAdapterRequestDevice(pimpl->adapter, &device_desc, device_callback);
        PumpUntil(pimpl->instance, device_request.done, "requestDevice");
        pimpl->device = device_request.handle;
        if (pimpl->device == nullptr) {
            throw std::runtime_error("WebGPU: device creation failed.");
        }

        pimpl->queue_info.graphicsQueue = wgpuDeviceGetQueue(pimpl->device);

        // What the device says it can do. The seam exposes four of these through
        // DeviceCapabilities; the rest are here so that a consumer that outgrows the
        // four has somewhere to look rather than a second query to write.
        pimpl->limits = WGPULimits{};
        if (!wgpuDeviceGetLimits(pimpl->device, &pimpl->limits)) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "WebGPU: the device did not report its limits.");
        }

        SDL_LogInfo(
            SDL_LOG_CATEGORY_RENDER,
            "WebGPU device ready for '%s'.",
            cfg.application_name.empty() ? "(unnamed)" : cfg.application_name.c_str()
        );
    }

    DeviceInterface::~DeviceInterface() {
        if (pimpl->device != nullptr) {
            wgpuDeviceRelease(pimpl->device);
        }
        if (pimpl->adapter != nullptr) {
            wgpuAdapterRelease(pimpl->adapter);
        }
        if (pimpl->instance != nullptr) {
            wgpuInstanceRelease(pimpl->instance);
        }
    }

    InstanceHandle DeviceInterface::GetInstance() const {
        return FromWgpuInstance(pimpl->instance);
    }

    SurfaceHandle DeviceInterface::GetSurface() const {
        // The surface is created by the presentation provider, which owns the window
        // configuration; the device interface has none of its own.
        return {};
    }

    PhysicalDeviceHandle DeviceInterface::GetPhysicalDevice() const {
        return FromWgpuAdapter(pimpl->adapter);
    }

    DeviceHandle DeviceInterface::GetDevice() const {
        return FromWgpuDevice(pimpl->device);
    }

    const QueueInfo &DeviceInterface::GetQueueInfo() const {
        return pimpl->queue_info;
    }

    uint32_t DeviceInterface::QueryLimit(PhysicalDeviceLimitInteger limit) const {
        // The limits block is read once at creation; these are the entries the seam names.
        switch (limit) {
        case PhysicalDeviceLimitInteger::MaxUniformBufferSize:
            return pimpl->limits.maxUniformBufferBindingSize;
        case PhysicalDeviceLimitInteger::MaxStorageBufferSize:
            return pimpl->limits.maxStorageBufferBindingSize;
        case PhysicalDeviceLimitInteger::UniformBufferOffsetAlignment:
            return pimpl->limits.minUniformBufferOffsetAlignment;
        case PhysicalDeviceLimitInteger::StorageBufferOffsetAlignment:
            return pimpl->limits.minStorageBufferOffsetAlignment;
        case PhysicalDeviceLimitInteger::AsyncTransferImageGranularityWidth:
        case PhysicalDeviceLimitInteger::AsyncTransferImageGranularityHeight:
        case PhysicalDeviceLimitInteger::AsyncTransferImageGranularityDepth:
            // No transfer granularity: a copy is expressed in texels and the device honours
            // the whole range.
            return 1;
        }
        return 0;
    }

    float DeviceInterface::QueryLimit(PhysicalDeviceLimitFloat) const {
        // The seam declares no float limits today; the enum is empty.
        return 0.0f;
    }

    SwapchainSupport DeviceInterface::GetSwapchainSupport() const {
        return SwapchainSupport{};
    }
} // namespace Engine::Rhi







