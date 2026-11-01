#include "Rhi/Device/DeviceInterface.h"

#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/Backend/WebGPU/WgpuStructs.h"

#include <SDL3/SDL.h>
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
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            static_cast<void>(what);
        }
    } // namespace

    struct DeviceInterface::impl {
        WGPUInstance instance{nullptr};
        WGPUAdapter adapter{nullptr};
        WGPUDevice device{nullptr};
        QueueInfo queue_info{};
    };

    DeviceInterface::DeviceInterface(DeviceConfiguration cfg) : pimpl(std::make_unique<impl>()) {
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

        Request<WGPUDevice> device_request{};
        WGPUDeviceDescriptor device_desc{};
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

    SwapchainSupport DeviceInterface::GetSwapchainSupport() const {
        return SwapchainSupport{};
    }
} // namespace Engine::Rhi



