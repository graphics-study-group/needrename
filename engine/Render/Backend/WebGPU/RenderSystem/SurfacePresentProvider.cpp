#include "Render/Backend/WebGPU/RenderSystem/SurfacePresentProvider.h"

#include <SDL3/SDL.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif
#include <webgpu/webgpu.h>

#if !defined(__EMSCRIPTEN__)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cstdint>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>

#include "Render/Backend/WebGPU/RenderSystem/PresentModePolicy.h"
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
        /// The pass that turns the final colour target into the surface's encoded image:
        /// a shader module, its bindings and one pipeline. Built on the first copy, when the
        /// surface format is known.
        WGPUShaderModule present_shader{nullptr};
        WGPUBindGroupLayout present_bind_group_layout{nullptr};
        WGPUPipelineLayout present_pipeline_layout{nullptr};
        WGPURenderPipeline present_pipeline{nullptr};
        WGPUSampler present_sampler{nullptr};
        /// @brief Whether pipeline creation has been tried and refused, so the failure is reported once.
        bool present_pipeline_failed{false};
        /// @brief The view of the acquired surface image the present pass draws into.
        WGPUTextureView current_view{nullptr};
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

        /// @brief Build the present pass on first use; false when the device refused it.
        bool EnsurePresentPipeline();
        /// @brief Draw the final colour target into the surface image; false when it could not run.
        bool RecordPresentPass(WGPUCommandEncoder encoder, const RenderTargetTexture &final_target);
    };

    namespace {
        /// @brief Whether a surface format makes the GPU apply the sRGB transfer function itself.
        bool IsSrgbFormat(WGPUTextureFormat format) {
            return format == WGPUTextureFormat_RGBA8UnormSrgb || format == WGPUTextureFormat_BGRA8UnormSrgb;
        }

        /// @brief A string view that spans a whole C string, for the labels the API takes.
        WGPUStringView Label(const char *text) {
            return WGPUStringView{text, SDL_strlen(text)};
        }

        /**
         * @brief The screen-space pass that presents the final colour target.
         *
         * The render graph leaves the final target in linear light: the lit pass writes an HDR
         * target and bloom copies it through. A surface's 8-bit image, though, is read by the
         * compositor as sRGB-encoded, so a raw copy hands it linear values and every frame comes
         * out too dark. Vulkan has the same problem and solves it with a blit into an sRGB
         * swapchain image; this interface's blit is a copy with no conversion, so the conversion
         * is a draw. `fs_encode` is the transfer function; `fs_linear` exists for the case where
         * the surface itself is an sRGB format and the fixed-function write already encodes.
         */
        constexpr const char *PRESENT_WGSL = R"(
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

@group(0) @binding(0) var source_texture: texture_2d<f32>;
@group(0) @binding(1) var source_sampler: sampler;

@vertex
fn vs_main(@builtin(vertex_index) vertex_index: u32) -> VertexOutput {
    // One oversized triangle covers the target without a vertex buffer.
    var positions = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
    let position = positions[vertex_index];
    var output: VertexOutput;
    output.position = vec4f(position, 0.0, 1.0);
    // NDC y points up and a texture's v points down, so v is flipped here: the image is
    // presented the right way up, not mirrored.
    output.uv = vec2f(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5);
    return output;
}

/// The sRGB transfer function, linear light to the encoding the surface is displayed through.
fn encode_srgb(linear: vec3f) -> vec3f {
    let low = linear * 12.92;
    let high = 1.055 * pow(max(linear, vec3f(0.0)), vec3f(1.0 / 2.4)) - 0.055;
    return select(high, low, linear <= vec3f(0.0031308));
}

@fragment
fn fs_encode(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(source_texture, source_sampler, input.uv);
    return vec4f(encode_srgb(color.rgb), 1.0);
}

@fragment
fn fs_linear(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(source_texture, source_sampler, input.uv);
    return vec4f(color.rgb, 1.0);
}
)";
    } // namespace

    bool SurfacePresentProvider::impl::EnsurePresentPipeline() {
        if (present_pipeline != nullptr) {
            return true;
        }
        if (present_pipeline_failed) {
            return false;
        }

        WGPUShaderSourceWGSL source{};
        source.chain.sType = WGPUSType_ShaderSourceWGSL;
        source.code = WGPUStringView{PRESENT_WGSL, SDL_strlen(PRESENT_WGSL)};
        WGPUShaderModuleDescriptor shader_desc{};
        shader_desc.nextInChain = &source.chain;
        shader_desc.label = Label("Present pass shader");
        present_shader = wgpuDeviceCreateShaderModule(device, &shader_desc);

        WGPUBindGroupLayoutEntry entries[2]{};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Fragment;
        entries[0].texture.sampleType = WGPUTextureSampleType_Float;
        entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Fragment;
        entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

        WGPUBindGroupLayoutDescriptor layout_desc{};
        layout_desc.entryCount = 2;
        layout_desc.entries = entries;
        layout_desc.label = Label("Present pass bind group layout");
        present_bind_group_layout = wgpuDeviceCreateBindGroupLayout(device, &layout_desc);

        WGPUPipelineLayoutDescriptor pipeline_layout_desc{};
        pipeline_layout_desc.bindGroupLayoutCount = 1;
        pipeline_layout_desc.bindGroupLayouts = &present_bind_group_layout;
        pipeline_layout_desc.label = Label("Present pass pipeline layout");
        present_pipeline_layout = wgpuDeviceCreatePipelineLayout(device, &pipeline_layout_desc);

        WGPUSamplerDescriptor sampler_desc{};
        sampler_desc.addressModeU = WGPUAddressMode_ClampToEdge;
        sampler_desc.addressModeV = WGPUAddressMode_ClampToEdge;
        sampler_desc.addressModeW = WGPUAddressMode_ClampToEdge;
        sampler_desc.magFilter = WGPUFilterMode_Linear;
        sampler_desc.minFilter = WGPUFilterMode_Linear;
        sampler_desc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        sampler_desc.lodMinClamp = 0.0f;
        sampler_desc.lodMaxClamp = 0.0f;
        sampler_desc.maxAnisotropy = 1;
        sampler_desc.label = Label("Present pass sampler");
        present_sampler = wgpuDeviceCreateSampler(device, &sampler_desc);

        WGPUVertexState vertex{};
        vertex.module = present_shader;
        vertex.entryPoint = WGPUStringView{"vs_main", 7};
        vertex.bufferCount = 0;
        vertex.buffers = nullptr;

        WGPUColorTargetState color_target{};
        color_target.format = format;
        color_target.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragment{};
        fragment.module = present_shader;
        const char *fragment_entry = IsSrgbFormat(format) ? "fs_linear" : "fs_encode";
        fragment.entryPoint = WGPUStringView{fragment_entry, SDL_strlen(fragment_entry)};
        fragment.targetCount = 1;
        fragment.targets = &color_target;

        WGPURenderPipelineDescriptor pipeline_desc{};
        pipeline_desc.layout = present_pipeline_layout;
        pipeline_desc.vertex = vertex;
        pipeline_desc.fragment = &fragment;
        pipeline_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipeline_desc.primitive.cullMode = WGPUCullMode_None;
        pipeline_desc.multisample.count = 1;
        pipeline_desc.multisample.mask = 0xFFFFFFFF;
        present_pipeline = wgpuDeviceCreateRenderPipeline(device, &pipeline_desc);

        if (present_pipeline == nullptr) {
            present_pipeline_failed = true;
            SDL_LogError(
                SDL_LOG_CATEGORY_RENDER,
                "WebGPU surface: the present pass could not be created; the frame is copied without "
                "sRGB encoding."
            );
            return false;
        }
        return true;
    }

    bool SurfacePresentProvider::impl::RecordPresentPass(
        WGPUCommandEncoder encoder, const RenderTargetTexture &final_target
    ) {
        if (!EnsurePresentPipeline() || current == nullptr) {
            return false;
        }

        if (current_view != nullptr) {
            wgpuTextureViewRelease(current_view);
            current_view = nullptr;
        }
        WGPUTextureViewDescriptor view_desc{};
        view_desc.format = format;
        view_desc.dimension = WGPUTextureViewDimension_2D;
        view_desc.baseMipLevel = 0;
        view_desc.mipLevelCount = 1;
        view_desc.baseArrayLayer = 0;
        view_desc.arrayLayerCount = 1;
        view_desc.aspect = WGPUTextureAspect_All;
        current_view = wgpuTextureCreateView(current, &view_desc);
        if (current_view == nullptr) {
            return false;
        }

        WGPUBindGroupEntry bind_group_entries[2]{};
        bind_group_entries[0].binding = 0;
        bind_group_entries[0].textureView = Rhi::ToWgpuTextureView(final_target.GetImageView());
        bind_group_entries[1].binding = 1;
        bind_group_entries[1].sampler = present_sampler;

        WGPUBindGroupDescriptor bind_group_desc{};
        bind_group_desc.layout = present_bind_group_layout;
        bind_group_desc.entryCount = 2;
        bind_group_desc.entries = bind_group_entries;
        WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(device, &bind_group_desc);
        if (bind_group == nullptr) {
            return false;
        }

        // The INIT macro rather than `{}`: `depthSlice` defaults to zero, which names a slice of a
        // 3D view rather than "no slice", and the validator rejects it on a 2D attachment.
        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view = current_view;
        color.loadOp = WGPULoadOp_Clear;
        color.storeOp = WGPUStoreOp_Store;
        color.clearValue = WGPUColor{0.0, 0.0, 0.0, 1.0};

        WGPURenderPassDescriptor pass_desc{};
        pass_desc.colorAttachmentCount = 1;
        pass_desc.colorAttachments = &color;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
        if (pass != nullptr) {
            wgpuRenderPassEncoderSetPipeline(pass, present_pipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        }
        wgpuBindGroupRelease(bind_group);
        return pass != nullptr;
    }

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
    ) : pimpl(std::make_unique<impl>()) {
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
            // Prefer a non-sRGB format: the present pass applies the transfer function itself,
            // and an sRGB surface would encode the same values a second time. RGBA first,
            // because the colour targets the render graphs produce are RGBA.
            const WGPUTextureFormat preferred[] = {
                WGPUTextureFormat_RGBA8Unorm,
                WGPUTextureFormat_BGRA8Unorm,
            };
            bool chosen = false;
            for (const WGPUTextureFormat wanted : preferred) {
                for (size_t index = 0; index < capabilities.formatCount && !chosen; index++) {
                    if (capabilities.formats[index] == wanted) {
                        pimpl->format = wanted;
                        chosen = true;
                    }
                }
                if (chosen) break;
            }
        }
        if (capabilities.presentModeCount > 0) {
            // Take the first mode the surface advertises only as a last resort: the policy is
            // what decides between Mailbox and FIFO, and it is applied to the advertised list.
#if defined(__EMSCRIPTEN__)
            constexpr PresentTarget k_target = PresentTarget::Browser;
#else
            constexpr PresentTarget k_target = PresentTarget::Native;
#endif
            pimpl->present_mode = SelectPresentMode(
                k_target, std::span<const WGPUPresentMode>(capabilities.presentModes, capabilities.presentModeCount)
            );
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
        if (capture_request != nullptr && capture_request[0] != '\0') {
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
            "WebGPU surface: %ux%u, format %d, present %s (%d), alpha %d, %zu formats offered",
            pimpl->extent.width,
            pimpl->extent.height,
            static_cast<int>(pimpl->format),
            PresentModeName(pimpl->present_mode),
            static_cast<int>(pimpl->present_mode),
            static_cast<int>(pimpl->alpha_mode),
            capabilities.formatCount
        );
    }

    namespace {
        /// @brief Write a tight 24-bit BMP, converting the surface's channel order.
        void WriteBmp(
            const std::string &path,
            const std::byte *pixels,
            uint32_t width,
            uint32_t height,
            uint32_t row_pitch,
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
                'B',
                'M',
                byte_at(file_size, 0),
                byte_at(file_size, 8),
                byte_at(file_size, 16),
                byte_at(file_size, 24),
                0,
                0,
                0,
                0,
                54,
                0,
                0,
                0,
                40,
                0,
                0,
                0,
                byte_at(width, 0),
                byte_at(width, 8),
                byte_at(width, 16),
                byte_at(width, 24),
                byte_at(height, 0),
                byte_at(height, 8),
                byte_at(height, 16),
                byte_at(height, 24),
                1,
                0,
                24,
                0,
                0,
                0,
                0,
                0,
                byte_at(image_bytes, 0),
                byte_at(image_bytes, 8),
                byte_at(image_bytes, 16),
                byte_at(image_bytes, 24),
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0
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
        if (pimpl->present_pipeline != nullptr) {
            wgpuRenderPipelineRelease(pimpl->present_pipeline);
            pimpl->present_pipeline = nullptr;
        }
        if (pimpl->present_pipeline_layout != nullptr) {
            wgpuPipelineLayoutRelease(pimpl->present_pipeline_layout);
            pimpl->present_pipeline_layout = nullptr;
        }
        if (pimpl->present_bind_group_layout != nullptr) {
            wgpuBindGroupLayoutRelease(pimpl->present_bind_group_layout);
            pimpl->present_bind_group_layout = nullptr;
        }
        if (pimpl->present_sampler != nullptr) {
            wgpuSamplerRelease(pimpl->present_sampler);
            pimpl->present_sampler = nullptr;
        }
        if (pimpl->present_shader != nullptr) {
            wgpuShaderModuleRelease(pimpl->present_shader);
            pimpl->present_shader = nullptr;
        }
        if (pimpl->current_view != nullptr) {
            wgpuTextureViewRelease(pimpl->current_view);
            pimpl->current_view = nullptr;
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
        // The seam's format list spells the encoding as well, because a caller that builds a
        // pipeline against this target needs the format the surface actually has. Anything else
        // the surface might report is presented as the 8-bit unorm it most resembles.
        switch (pimpl->format) {
        case WGPUTextureFormat_RGBA8Unorm:
            return Rhi::ImageFormat::R8G8B8A8UNorm;
        case WGPUTextureFormat_RGBA8UnormSrgb:
            return Rhi::ImageFormat::R8G8B8A8SRGB;
        case WGPUTextureFormat_BGRA8Unorm:
            return Rhi::ImageFormat::B8G8R8A8UNorm;
        case WGPUTextureFormat_BGRA8UnormSrgb:
            return Rhi::ImageFormat::B8G8R8A8SRGB;
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
        if (pimpl->current_view != nullptr) {
            wgpuTextureViewRelease(pimpl->current_view);
            pimpl->current_view = nullptr;
        }
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
#if defined(__EMSCRIPTEN__)
            // A page answers a map on its own schedule, and a loop that only pumps events never
            // lets that schedule run: the sleep yields to the browser, which is what delivers the
            // callback.
            emscripten_sleep(0);
#endif
            if (instance != nullptr) {
                wgpuInstanceProcessEvents(instance);
            }
        }
        if (!mapped_ok) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "WebGPU surface: the frame capture could not be read back.");
            return;
        }
        // Read-mode mapping: this port only hands out the const range for it.
        const auto *pixels =
            static_cast<const std::byte *>(wgpuBufferGetConstMappedRange(pimpl->capture, 0, pimpl->capture_size));
        const bool bgra =
            pimpl->format == WGPUTextureFormat_BGRA8Unorm || pimpl->format == WGPUTextureFormat_BGRA8UnormSrgb;
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
        Rhi::DeviceHandle, const RenderTargetTexture &final_rtt, uint32_t, Rhi::MemoryAccessTypeImageBits
    ) {
        // The frame this provider copied out on its previous call has been presented by now, so
        // the surface image it was taken from is free to be read back.
#if defined(__EMSCRIPTEN__)
        {
            static bool logged_capture_state = false;
            if (!logged_capture_state) {
                logged_capture_state = true;
                SDL_LogInfo(
                    SDL_LOG_CATEGORY_RENDER,
                    "WebGPU surface (web): capture %s, %llu byte(s), pitch %u",
                    pimpl->capture != nullptr ? "present" : "absent",
                    static_cast<unsigned long long>(pimpl->capture_size),
                    pimpl->capture_row_pitch
                );
            }
        }
#endif
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
        // The final target holds linear light while the compositor reads the surface as sRGB.
        // A raw copy would hand it linear values and every frame would come out too dark, so the
        // frame is presented through a pass that applies the transfer function - the same
        // conversion Vulkan gets from a blit into an sRGB swapchain image.
        if (!pimpl->RecordPresentPass(encoder, final_rtt)) {
            // A refused pass still presents a frame; only the encoding is missing, and the
            // failure has already been reported.
            wgpuCommandEncoderCopyTextureToTexture(encoder, &source, &destination, &extent);
        }

        if (pimpl->capture != nullptr) {
            // What is read back is normally the surface image - the frame the window shows. A
            // diagnosis can ask for the render target instead, which says whether a black window
            // is a recording that drew nothing or a copy that lost what was drawn.
            const bool capture_target = SDL_getenv("NEEDRENAME_CAPTURE_SOURCE") != nullptr
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
