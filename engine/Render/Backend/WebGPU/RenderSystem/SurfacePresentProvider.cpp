#include "Render/Backend/WebGPU/RenderSystem/SurfacePresentProvider.h"

#include <SDL3/SDL.h>
#include <webgpu/webgpu.h>

#if !defined(__EMSCRIPTEN__)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>

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
        /// The finished copy of the final colour target into the surface image. Kept until the
        /// next acquire, which is when the present that waited on it has completed.
        WGPUCommandBuffer copy_buffer{nullptr};
        /// A read-back copy of the surface image itself, so a run can be looked at rather than
        /// described. Only created when the capture environment variable names a file.
        WGPUBuffer capture{nullptr};
        uint64_t capture_size{0};
        uint32_t capture_row_pitch{0};
        std::string capture_path{};
        bool capture_pending{false};
        /// @brief Whether the capture exists only to report whether the frame has any colour.
        bool capture_probe{false};
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
            // Prefer the format the render targets can be copied into without a channel
            // conversion: a copy between an RGBA target and a BGRA surface is not copy-compatible,
            // and a surface that offers both costs nothing to choose the matching one.
            for (size_t index = 0; index < capabilities.formatCount; index++) {
                const WGPUTextureFormat offered = capabilities.formats[index];
                if (offered == WGPUTextureFormat_RGBA8Unorm || offered == WGPUTextureFormat_RGBA8UnormSrgb) {
                    pimpl->format = offered;
                    break;
                }
            }
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
        // CopySrc is what the optional frame capture reads the presented image through; a surface
        // texture that offers neither copy direction would also be one this backend could not draw
        // into twice, so both are requested.
        configuration.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;
        configuration.width = pimpl->extent.width;
        configuration.height = pimpl->extent.height;
        configuration.presentMode = pimpl->present_mode;
        configuration.alphaMode = pimpl->alpha_mode;

        wgpuSurfaceConfigure(pimpl->surface, &configuration);

        // A frame dump on request: the surface image is copied out with the frame's own copy,
        // and written once the present that read it has completed. The environment variable names
        // the file; without it nothing here costs anything.
        const char *capture_request = SDL_getenv("NEEDRENAME_PRESENT_CAPTURE");
#if defined(__EMSCRIPTEN__)
        // A page has no environment to set, and no file to hand back, but it can still answer the
        // one question a black canvas asks: does the frame it copied out have any colour in it?
        if (capture_request == nullptr || capture_request[0] == '\0') {
            capture_request = "";
            pimpl->capture_probe = true;
        }
#endif
        if (capture_request != nullptr && (capture_request[0] != '\0' || pimpl->capture_probe)) {
            pimpl->capture_path = capture_request;
            pimpl->capture_row_pitch = (pimpl->extent.width * 4u + 255u) / 256u * 256u;
            pimpl->capture_size = static_cast<uint64_t>(pimpl->capture_row_pitch) * pimpl->extent.height;
            WGPUBufferDescriptor capture_desc{};
            capture_desc.size = pimpl->capture_size;
            capture_desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
            capture_desc.label = WGPUStringView{"Present capture", 16};
            pimpl->capture = wgpuDeviceCreateBuffer(pimpl->device, &capture_desc);
            if (pimpl->capture == nullptr) {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "WebGPU surface: the frame capture buffer could not be created.");
            }
        }

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

    namespace {
        /// @brief Write a tight 24-bit BMP, converting the surface's channel order.
        void WriteBmp(
            const std::string &path, const std::byte *pixels, uint32_t width, uint32_t height, uint32_t row_pitch,
            bool bgra
        ) {
            const uint32_t row_bytes = width * 3u;
            const uint32_t padding = (4u - (row_bytes % 4u)) % 4u;
            const uint32_t image_bytes = (row_bytes + padding) * height;
            std::ofstream file(path, std::ios::binary);
            if (!file.is_open()) {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "WebGPU surface: cannot open %s", path.c_str());
                return;
            }
            const uint32_t file_size = 54u + image_bytes;
            const auto byte_at = [](uint32_t value, int shift) {
                return static_cast<uint8_t>((value >> shift) & 0xFFu);
            };
            const uint8_t header[54] = {
                'B', 'M', byte_at(file_size, 0), byte_at(file_size, 8), byte_at(file_size, 16), byte_at(file_size, 24),
                0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0,
                byte_at(width, 0), byte_at(width, 8), byte_at(width, 16), byte_at(width, 24),
                byte_at(height, 0), byte_at(height, 8), byte_at(height, 16), byte_at(height, 24),
                1, 0, 24, 0, 0, 0, 0, 0,
                byte_at(image_bytes, 0), byte_at(image_bytes, 8), byte_at(image_bytes, 16), byte_at(image_bytes, 24),
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
            };
            file.write(reinterpret_cast<const char *>(header), sizeof(header));
            for (uint32_t y = 0; y < height; y++) {
                const uint32_t source_row = height - 1u - y;
                const std::byte *row = pixels + static_cast<size_t>(source_row) * row_pitch;
                for (uint32_t x = 0; x < width; x++) {
                    const size_t at = static_cast<size_t>(x) * 4u;
                    const char red = static_cast<char>(std::to_integer<uint8_t>(row[at + (bgra ? 2u : 0u)]));
                    const char green = static_cast<char>(std::to_integer<uint8_t>(row[at + 1u]));
                    const char blue = static_cast<char>(std::to_integer<uint8_t>(row[at + (bgra ? 0u : 2u)]));
                    const char bgr[3] = {blue, green, red};
                    file.write(bgr, 3);
                }
                for (uint32_t pad = 0; pad < padding; pad++) {
                    file.put('\0');
                }
            }
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "WebGPU surface: frame captured to %s", path.c_str());
        }
    } // namespace

    SurfacePresentProvider::~SurfacePresentProvider() {
        if (pimpl->capture != nullptr) {
            wgpuBufferRelease(pimpl->capture);
            pimpl->capture = nullptr;
        }
        if (pimpl->copy_buffer != nullptr) {
            wgpuCommandBufferRelease(pimpl->copy_buffer);
            pimpl->copy_buffer = nullptr;
        }
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

    /// @brief Read a finished capture back and write it out, before it is reused.
    void SurfacePresentProvider::CaptureFinishedFrame() {
        if (!pimpl->capture_pending || pimpl->capture == nullptr) {
            return;
        }
        pimpl->capture_pending = false;

        bool done = false;
        bool mapped_ok = false;
        struct State {
            bool *done;
            bool *ok;
        } state{&done, &mapped_ok};
        WGPUBufferMapCallbackInfo info{};
        info.mode = WGPUCallbackMode_AllowProcessEvents;
        info.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void *userdata1, void *) {
            auto *result = static_cast<State *>(userdata1);
            *result->done = true;
            *result->ok = status == WGPUMapAsyncStatus_Success;
        };
        info.userdata1 = &state;
        wgpuBufferMapAsync(pimpl->capture, WGPUMapMode_Read, 0, pimpl->capture_size, info);

        const WGPUInstance instance = pimpl->instance;
        while (!done) {
            if (instance != nullptr) {
                wgpuInstanceProcessEvents(instance);
            }
        }
        if (!mapped_ok) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "WebGPU surface: the frame capture could not be read back.");
            return;
        }
        const auto *pixels = static_cast<const std::byte *>(
            wgpuBufferGetMappedRange(pimpl->capture, 0, pimpl->capture_size)
        );
        const bool bgra = pimpl->format == WGPUTextureFormat_BGRA8Unorm
                          || pimpl->format == WGPUTextureFormat_BGRA8UnormSrgb;
#if defined(__EMSCRIPTEN__)
        // A page cannot hand a file back, so what it can say is whether the captured frame has any
        // colour in it at all: a black canvas that is black in the capture is a recording that drew
        // nothing, and one that is not is a presentation problem.
        {
            static size_t reported = 0;
            if (reported < 4) {
                reported++;
                uint8_t brightest = 0;
                for (size_t index = 0; index < pimpl->capture_size; index++) {
                    const uint8_t value = std::to_integer<uint8_t>(pixels[index]);
                    if (value > brightest) {
                        brightest = value;
                    }
                }
                SDL_LogInfo(
                    SDL_LOG_CATEGORY_RENDER,
                    "WebGPU surface (web): captured frame %zu, brightest byte %u",
                    reported,
                    static_cast<unsigned>(brightest)
                );
            }
        }
#endif
        if (!pimpl->capture_path.empty()) {
            WriteBmp(
                pimpl->capture_path, pixels, pimpl->extent.width, pimpl->extent.height, pimpl->capture_row_pitch, bgra
            );
        }
        wgpuBufferUnmap(pimpl->capture);
    }

    Rhi::CommandBufferHandle SurfacePresentProvider::PrepareCopy(
        Rhi::DeviceHandle,
        const RenderTargetTexture &final_rtt,
        uint32_t,
        Rhi::MemoryAccessTypeImageBits
    ) {
        // The frame this provider copied out on its previous call has been presented by now, so
        // the surface image it was taken from is free to be read back.
        CaptureFinishedFrame();
        // One surface image, acquired per frame, so the copy of the previous frame has completed
        // by the time the next acquire returns it: this is where it is released.
        if (pimpl->copy_buffer != nullptr) {
            wgpuCommandBufferRelease(pimpl->copy_buffer);
            pimpl->copy_buffer = nullptr;
        }
        if (pimpl->current == nullptr) {
            return {};
        }

        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(pimpl->device, nullptr);
        if (encoder == nullptr) {
            return {};
        }

        WGPUTexelCopyTextureInfo source{};
        source.texture = Rhi::ToWgpuTexture(final_rtt.GetImage());
        source.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyTextureInfo destination{};
        destination.texture = pimpl->current;
        destination.aspect = WGPUTextureAspect_All;

        const WGPUExtent3D extent{pimpl->extent.width, pimpl->extent.height, 1};
        wgpuCommandEncoderCopyTextureToTexture(encoder, &source, &destination, &extent);

        if (pimpl->capture != nullptr) {
            // What is read back is normally the surface image - the frame the window shows. A
            // diagnosis can ask for the render target instead, which says whether a black window
            // is a recording that drew nothing or a copy that lost what was drawn.
            const bool capture_target =
                SDL_getenv("NEEDRENAME_CAPTURE_SOURCE") != nullptr
                && std::string_view{SDL_getenv("NEEDRENAME_CAPTURE_SOURCE")} == "target";
            WGPUTexelCopyBufferInfo capture_destination{};
            capture_destination.buffer = pimpl->capture;
            capture_destination.layout.bytesPerRow = pimpl->capture_row_pitch;
            capture_destination.layout.rowsPerImage = pimpl->extent.height;
            wgpuCommandEncoderCopyTextureToBuffer(
                encoder, capture_target ? &source : &destination, &capture_destination, &extent
            );
            pimpl->capture_pending = true;
        }

        pimpl->copy_buffer = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuCommandEncoderRelease(encoder);
        if (pimpl->copy_buffer == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "WebGPU surface: the present copy could not be finished.");
            return {};
        }
        return Rhi::FromWgpuFinishedBuffer(pimpl->copy_buffer);
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
        // CopySrc is what the optional frame capture reads the presented image through; a surface
        // texture that offers neither copy direction would also be one this backend could not draw
        // into twice, so both are requested.
        configuration.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;
        configuration.width = pimpl->extent.width;
        configuration.height = pimpl->extent.height;
        configuration.presentMode = pimpl->present_mode;
        configuration.alphaMode = pimpl->alpha_mode;

        wgpuSurfaceConfigure(pimpl->surface, &configuration);
    }
} // namespace Engine::RenderSystemState