#include "Render/Backend/WebGPU/RenderSystem/SurfacePresentProvider.h"

#include <SDL3/SDL.h>
#include <webgpu/webgpu.h>

#if !defined(__EMSCRIPTEN__)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <stdexcept>

#include "Render/Resource/RenderTargetTexture.h"
#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/Device/DeviceInterface.h"
#include "Rhi/Texture/ImageUtils.h"

namespace Engine::RenderSystemState {
    struct SurfacePresentProvider::impl {
        WGPUInstance instance{nullptr};
        WGPUAdapter adapter{nullptr};
        WGPUSurface surface{nullptr};
        WGPUDevice device{nullptr};
        WGPUTextureFormat format{WGPUTextureFormat_BGRA8Unorm};
        WGPUPresentMode present_mode{WGPUPresentMode_Fifo};
        WGPUCompositeAlphaMode alpha_mode{WGPUCompositeAlphaMode_Opaque};
        Rhi::Extent2D extent{};
        WGPUTexture current{nullptr};
    };

    namespace {
        /// Where the surface is, per platform. A desktop window names its HWND; a page names the
        /// canvas the shell laid out.
        WGPUSurface CreateSurface(WGPUInstance instance, SDL_Window *window) {
#if defined(__EMSCRIPTEN__)
            static_cast<void>(window);
            WGPUEmscriptenSurfaceSourceCanvasHTMLSelector source{};
            source.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
            const char *selector = "#canvas";
            source.selector.data = selector;
            source.selector.length = SDL_strlen(selector);

            WGPUSurfaceDescriptor descriptor{};
            descriptor.nextInChain = &source.chain;
            return wgpuInstanceCreateSurface(instance, &descriptor);
#else
            if (window == nullptr) {
                return nullptr;
            }
            SDL_PropertiesID properties = SDL_GetWindowProperties(window);
            void *hwnd = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
            if (hwnd == nullptr) {
                return nullptr;
            }

            WGPUSurfaceSourceWindowsHWND source{};
            source.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
            source.hinstance = GetModuleHandle(nullptr);
            source.hwnd = hwnd;

            WGPUSurfaceDescriptor descriptor{};
            descriptor.nextInChain = &source.chain;
            return wgpuInstanceCreateSurface(instance, &descriptor);
#endif
        }
    } // namespace

    SurfacePresentProvider::SurfacePresentProvider(
        const Rhi::DeviceInterface &device_interface, SDL_Window *window, Rhi::Extent2D extent
    ) :
        pimpl(std::make_unique<impl>()) {
        pimpl->instance = Rhi::ToWgpuInstance(device_interface.GetInstance());
        pimpl->adapter = Rhi::ToWgpuAdapter(device_interface.GetPhysicalDevice());
        pimpl->device = Rhi::ToWgpuDevice(device_interface.GetDevice());
        pimpl->extent = extent;

        if (pimpl->instance == nullptr || pimpl->adapter == nullptr) {
            throw std::logic_error("SurfacePresentProvider: the device has no instance or adapter.");
        }

        pimpl->surface = CreateSurface(pimpl->instance, window);
        if (pimpl->surface == nullptr) {
            throw std::logic_error("SurfacePresentProvider: the window has no WebGPU surface.");
        }

        // Ask the surface what it accepts instead of assuming. A canvas and a window do not offer
        // the same list, and configuring with a format the surface does not have is a validation
        // error rather than a fallback.
        WGPUSurfaceCapabilities capabilities{};
        wgpuSurfaceGetCapabilities(pimpl->surface, pimpl->adapter, &capabilities);
        if (capabilities.formatCount > 0) {
            pimpl->format = capabilities.formats[0];
        }
        if (capabilities.presentModeCount > 0) {
            pimpl->present_mode = capabilities.presentModes[0];
        }
        if (capabilities.alphaModeCount > 0) {
            pimpl->alpha_mode = capabilities.alphaModes[0];
        }

        WGPUSurfaceConfiguration configuration{};
        configuration.device = pimpl->device;
        configuration.format = pimpl->format;
        configuration.usage = WGPUTextureUsage_RenderAttachment;
        configuration.width = pimpl->extent.width;
        configuration.height = pimpl->extent.height;
        configuration.presentMode = pimpl->present_mode;
        configuration.alphaMode = pimpl->alpha_mode;

        wgpuSurfaceConfigure(pimpl->surface, &configuration);

        SDL_LogInfo(
            SDL_LOG_CATEGORY_RENDER,
            "WebGPU surface: %ux%u, format %d, present %d, alpha %d, %zu formats offered",
            pimpl->extent.width,
            pimpl->extent.height,
            static_cast<int>(pimpl->format),
            static_cast<int>(pimpl->present_mode),
            static_cast<int>(pimpl->alpha_mode),
            capabilities.formatCount
        );
    }

    SurfacePresentProvider::~SurfacePresentProvider() {
        if (pimpl->current != nullptr) {
            wgpuTextureRelease(pimpl->current);
        }
        if (pimpl->surface != nullptr) {
            wgpuSurfaceRelease(pimpl->surface);
        }
    }

    Rhi::Extent2D SurfacePresentProvider::GetExtent() const {
        return pimpl->extent;
    }

    Rhi::ImageFormat SurfacePresentProvider::GetColorFormat() const {
        // The seam's format list spells the two formats a surface offers. Anything else the surface
        // might report is presented as the 8-bit unorm it most resembles.
        switch (pimpl->format) {
        case WGPUTextureFormat_RGBA8Unorm:
        case WGPUTextureFormat_RGBA8UnormSrgb:
            return Rhi::ImageFormat::R8G8B8A8UNorm;
        case WGPUTextureFormat_BGRA8Unorm:
        case WGPUTextureFormat_BGRA8UnormSrgb:
        default:
            return Rhi::ImageFormat::B8G8R8A8UNorm;
        }
    }

    uint32_t SurfacePresentProvider::GetImageCount() const {
        // Acquired per frame rather than rotated through, so the count is what the frame manager
        // needs to size its semaphores: one.
        return 1;
    }

    uint32_t SurfacePresentProvider::AcquireNextImage(Rhi::DeviceHandle, Rhi::SemaphoreHandle, uint64_t) {
        if (pimpl->current != nullptr) {
            wgpuTextureRelease(pimpl->current);
            pimpl->current = nullptr;
        }

        WGPUSurfaceTexture acquired{};
        wgpuSurfaceGetCurrentTexture(pimpl->surface, &acquired);
        if (acquired.status == WGPUSurfaceGetCurrentTextureStatus_Outdated
            || acquired.status == WGPUSurfaceGetCurrentTextureStatus_Lost) {
            // The surface no longer matches its configuration; the caller recreates and retries.
            return ~0u;
        }
        if (acquired.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal
            && acquired.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
            SDL_LogError(
                SDL_LOG_CATEGORY_RENDER, "WebGPU surface: acquire status %d", static_cast<int>(acquired.status)
            );
            return ~0u;
        }

        pimpl->current = acquired.texture;
        return 0;
    }

    Rhi::CommandBufferHandle SurfacePresentProvider::PrepareCopy(
        Rhi::DeviceHandle, const RenderTargetTexture &, uint32_t, Rhi::MemoryAccessTypeImageBits
    ) {
        // The copy is recorded by FrameManager through the seam once the destination texture is
        // reachable as a seam handle. Until the backend can hand a surface texture out as one, this
        // provider reports that it has no copy to contribute instead of returning a buffer that
        // records nothing.
        return {};
    }

    bool SurfacePresentProvider::Present(Rhi::DeviceHandle, uint32_t, Rhi::SemaphoreHandle) {
        if (pimpl->surface == nullptr) {
            return false;
        }
#if defined(__EMSCRIPTEN__)
        // A canvas is composited by the page on its next animation frame. The port rejects an
        // explicit present, and there is nothing for it to do.
        return false;
#else
        wgpuSurfacePresent(pimpl->surface);
        return false;
#endif
    }

    void SurfacePresentProvider::Recreate(Rhi::Extent2D new_extent) {
        if (new_extent.width == 0 || new_extent.height == 0) {
            return;
        }
        pimpl->extent = new_extent;

        WGPUSurfaceConfiguration configuration{};
        configuration.device = pimpl->device;
        configuration.format = pimpl->format;
        configuration.usage = WGPUTextureUsage_RenderAttachment;
        configuration.width = pimpl->extent.width;
        configuration.height = pimpl->extent.height;
        configuration.presentMode = pimpl->present_mode;
        configuration.alphaMode = pimpl->alpha_mode;

        wgpuSurfaceConfigure(pimpl->surface, &configuration);
    }
} // namespace Engine::RenderSystemState

