// A triangle drawn through `webgpu.h` with no engine in the way, whose colour is written by a
// compute dispatch recorded before the pass in the same command encoder.
//
// Two questions this program exists to answer, in the order they were asked:
//
//   1. Can a surface be acquired and presented at all on this target - on a canvas as well as on a
//      window? (This program started as the clear-only answer to that.)
//   2. Can the same program bind a storage buffer, dispatch a compute pass into it, and read the
//      result from a fragment shader? A dispatch that never ran leaves a black triangle rather than
//      a differently coloured one, so the frame itself is the evidence.
//
// The program is deliberately callback-driven on the web. A browser answers `requestAdapter` and
// `requestDevice` on its own event loop, and a page that waits for them in a loop inside `main`
// never yields the turn that would answer - the adapter arrives and the device never does. So the
// requests are issued from `main`, and everything after the device happens in its callback, driven
// by `emscripten_set_main_loop`. The desktop build uses the same callbacks and a plain frame loop.

#include <SDL3/SDL.h>
#include <webgpu/webgpu.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cstdint>
#include <cstdio>

namespace {
    constexpr uint32_t WINDOW_WIDTH = 800;
    constexpr uint32_t WINDOW_HEIGHT = 600;
    constexpr WGPUTextureFormat SURFACE_FORMAT = WGPUTextureFormat_BGRA8Unorm;
    /// Frames to present before the program reports and stops, so a headless run finishes.
    constexpr uint64_t FRAME_LIMIT = 30;

    // The triangle and the dispatch, one source string each. Both touch the same storage buffer:
    // the compute stage writes it and the fragment stage reads it, so the colour on screen is the
    // value the dispatch produced.
    constexpr const char *COMPUTE_WGSL = R"(
struct Colour { value : vec4f };
@group(0) @binding(0) var<storage, read_write> computed : Colour;

@compute @workgroup_size(1)
fn main() {
    computed.value = vec4f(0.95, 0.55, 0.15, 1.0);
}
)";

    constexpr const char *TRIANGLE_WGSL = R"(
struct Colour { value : vec4f };
// `read_write`, not `read`: the layout declares the binding writable for the compute stage, and a
// binding whose declared access disagrees with the layout is rejected at pipeline creation.
@group(0) @binding(0) var<storage, read_write> computed : Colour;

@vertex
fn vs_main(@builtin(vertex_index) index : u32) -> @builtin(position) vec4f {
    var positions = array<vec2f, 3>(
        vec2f( 0.0,  0.7),
        vec2f(-0.7, -0.5),
        vec2f( 0.7, -0.5)
    );
    return vec4f(positions[index], 0.0, 1.0);
}

@fragment
fn fs_main() -> @location(0) vec4f {
    return computed.value;
}
)";

    struct State {
        WGPUInstance instance{nullptr};
        WGPUSurface surface{nullptr};
        WGPUAdapter adapter{nullptr};
        WGPUDevice device{nullptr};
        WGPUQueue queue{nullptr};

        WGPUTextureFormat format{SURFACE_FORMAT};
        WGPUPresentMode present_mode{WGPUPresentMode_Fifo};
        WGPUCompositeAlphaMode alpha_mode{WGPUCompositeAlphaMode_Opaque};
        uint32_t width{WINDOW_WIDTH};
        uint32_t height{WINDOW_HEIGHT};

        WGPUBuffer colour_buffer{nullptr};
        WGPUBindGroup bind_group{nullptr};
        WGPUComputePipeline compute_pipeline{nullptr};
        WGPURenderPipeline triangle_pipeline{nullptr};

        uint64_t frames{0};
        bool ready{false};
        bool failed{false};
    };

    void RenderFrame(State &state);
    void StartRendering(State &state);

    bool adapter_done = false;
    bool device_done = false;

    void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView, void *userdata, void *) {
        SDL_Log("webgpu_clear: adapter status %d", static_cast<int>(status));
        if (status == WGPURequestAdapterStatus_Success) {
            static_cast<State *>(userdata)->adapter = adapter;
        }
        adapter_done = true;
        // A browser answers this between animation frames, so the device is requested from here
        // rather than from `main`, which has already returned by then.
        State *state = static_cast<State *>(userdata);
        if (state->adapter != nullptr && state->device == nullptr) {
            WGPURequestDeviceCallbackInfo device_cb{};
            device_cb.mode = WGPUCallbackMode_AllowProcessEvents;
            device_cb.callback = [](WGPURequestDeviceStatus device_status, WGPUDevice device, WGPUStringView, void *data, void *) {
                State *device_state = static_cast<State *>(data);
                SDL_Log("webgpu_clear: device status %d", static_cast<int>(device_status));
                if (device_status == WGPURequestDeviceStatus_Success) {
                    device_state->device = device;
                    device_state->queue = wgpuDeviceGetQueue(device);
                }
                device_done = true;
                if (device_state->device != nullptr) {
                    StartRendering(*device_state);
                }
            };
            device_cb.userdata1 = state;
            wgpuAdapterRequestDevice(state->adapter, nullptr, device_cb);
        }
    }

    void Pump(State &state, const char *what) {
        // A desktop implementation answers these requests on the calling thread's event pump, so a
        // bounded pump is enough. This is only reached on the desktop path: the web build lets the
        // page drive the callbacks instead.
        int iterations = 0;
        while (!device_done) {
            wgpuInstanceProcessEvents(state.instance);
            if (++iterations > 2000) {
                SDL_Log("webgpu_clear: %s did not resolve", what);
                return;
            }
            SDL_Delay(1);
        }
    }

    /// @brief What the browser's main loop calls each animation frame.
    void OnAnimationFrame(void *userdata) {
        RenderFrame(*static_cast<State *>(userdata));
    }
} // namespace

namespace {
    void StartRendering(State &state) {
        if (state.device == nullptr || state.ready) {
            return;
        }

        // Ask the surface what it can do instead of assuming a format. A canvas and a window do
        // not offer the same list, and configure asserts on a format it does not have.
        WGPUSurfaceCapabilities capabilities{};
        wgpuSurfaceGetCapabilities(state.surface, state.adapter, &capabilities);
        SDL_Log(
            "webgpu_clear: %zu formats, %zu present modes, %zu alpha modes",
            capabilities.formatCount,
            capabilities.presentModeCount,
            capabilities.alphaModeCount
        );
        if (capabilities.formatCount > 0) {
            state.format = capabilities.formats[0];
        }
        if (capabilities.presentModeCount > 0) {
            state.present_mode = capabilities.presentModes[0];
        }
        if (capabilities.alphaModeCount > 0) {
            state.alpha_mode = capabilities.alphaModes[0];
        }
        SDL_Log(
            "webgpu_clear: using format %d, present mode %d, alpha mode %d",
            static_cast<int>(state.format),
            static_cast<int>(state.present_mode),
            static_cast<int>(state.alpha_mode)
        );

        WGPUSurfaceConfiguration surface_config{};
        surface_config.device = state.device;
        surface_config.format = state.format;
        surface_config.usage = WGPUTextureUsage_RenderAttachment;
        surface_config.width = state.width;
        surface_config.height = state.height;
        surface_config.presentMode = state.present_mode;
        // Opaque, not Auto: an auto alpha lets the compositor decide, and a window whose contents
        // are blended with nothing underneath is exactly where a black result comes from.
        surface_config.alphaMode = state.alpha_mode;
        wgpuSurfaceConfigure(state.surface, &surface_config);
        SDL_Log("webgpu_clear: surface configured %ux%u", state.width, state.height);

        const uint64_t colour_bytes = 16;
        WGPUBufferDescriptor buffer_desc{};
        buffer_desc.size = colour_bytes;
        buffer_desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        state.colour_buffer = wgpuDeviceCreateBuffer(state.device, &buffer_desc);

        WGPUShaderSourceWGSL compute_source{};
        compute_source.chain.sType = WGPUSType_ShaderSourceWGSL;
        compute_source.code = WGPUStringView{COMPUTE_WGSL, SDL_strlen(COMPUTE_WGSL)};
        WGPUShaderModuleDescriptor compute_module_desc{};
        compute_module_desc.nextInChain = &compute_source.chain;
        compute_module_desc.label = WGPUStringView{"spike compute", 13};
        WGPUShaderModule compute_module = wgpuDeviceCreateShaderModule(state.device, &compute_module_desc);

        WGPUShaderSourceWGSL triangle_source{};
        triangle_source.chain.sType = WGPUSType_ShaderSourceWGSL;
        triangle_source.code = WGPUStringView{TRIANGLE_WGSL, SDL_strlen(TRIANGLE_WGSL)};
        WGPUShaderModuleDescriptor triangle_module_desc{};
        triangle_module_desc.nextInChain = &triangle_source.chain;
        triangle_module_desc.label = WGPUStringView{"spike triangle", 14};
        WGPUShaderModule triangle_module = wgpuDeviceCreateShaderModule(state.device, &triangle_module_desc);

        WGPUBindGroupLayoutEntry layout_entry{};
        layout_entry.binding = 0;
        layout_entry.visibility = WGPUShaderStage_Compute | WGPUShaderStage_Fragment;
        layout_entry.buffer.type = WGPUBufferBindingType_Storage;
        WGPUBindGroupLayoutDescriptor layout_desc{};
        layout_desc.entryCount = 1;
        layout_desc.entries = &layout_entry;
        WGPUBindGroupLayout bind_group_layout = wgpuDeviceCreateBindGroupLayout(state.device, &layout_desc);

        WGPUPipelineLayoutDescriptor pipeline_layout_desc{};
        pipeline_layout_desc.bindGroupLayoutCount = 1;
        pipeline_layout_desc.bindGroupLayouts = &bind_group_layout;
        WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(state.device, &pipeline_layout_desc);

        WGPUBindGroupEntry bind_entry{};
        bind_entry.binding = 0;
        bind_entry.buffer = state.colour_buffer;
        bind_entry.offset = 0;
        bind_entry.size = colour_bytes;
        WGPUBindGroupDescriptor bind_group_desc{};
        bind_group_desc.layout = bind_group_layout;
        bind_group_desc.entryCount = 1;
        bind_group_desc.entries = &bind_entry;
        state.bind_group = wgpuDeviceCreateBindGroup(state.device, &bind_group_desc);

        WGPUComputePipelineDescriptor compute_pipeline_desc{};
        compute_pipeline_desc.layout = pipeline_layout;
        compute_pipeline_desc.compute.module = compute_module;
        compute_pipeline_desc.compute.entryPoint = WGPUStringView{"main", 4};
        state.compute_pipeline = wgpuDeviceCreateComputePipeline(state.device, &compute_pipeline_desc);

        // A multisample count is required and zero is rejected; one is the single-sample path.
        WGPUMultisampleState multisample{};
        multisample.count = 1;
        multisample.mask = 0xFFFFFFFFu;

        WGPUColorTargetState target{};
        target.format = state.format;
        target.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragment{};
        fragment.module = triangle_module;
        fragment.entryPoint = WGPUStringView{"fs_main", 7};
        fragment.targetCount = 1;
        fragment.targets = &target;

        WGPURenderPipelineDescriptor render_pipeline_desc{};
        render_pipeline_desc.layout = pipeline_layout;
        render_pipeline_desc.vertex.module = triangle_module;
        render_pipeline_desc.vertex.entryPoint = WGPUStringView{"vs_main", 7};
        render_pipeline_desc.fragment = &fragment;
        render_pipeline_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        render_pipeline_desc.multisample = multisample;
        state.triangle_pipeline = wgpuDeviceCreateRenderPipeline(state.device, &render_pipeline_desc);

        if (state.compute_pipeline == nullptr || state.triangle_pipeline == nullptr) {
            SDL_Log("webgpu_clear: pipeline creation failed");
            state.failed = true;
            return;
        }
        SDL_Log("webgpu_clear: compute and triangle pipelines created");
        state.ready = true;
    }
    void RenderFrame(State &state) {
        // Pumped before the ready check, not after: the adapter and device callbacks are delivered
        // by this pump, so a first frame that returned early would be the reason they never arrive.
        wgpuInstanceProcessEvents(state.instance);
        if (!state.ready || state.failed) {
            return;
        }

        WGPUSurfaceTexture acquired{};
        wgpuSurfaceGetCurrentTexture(state.surface, &acquired);
        if (acquired.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal
            && acquired.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
            SDL_Log("webgpu_clear: acquire status %d", static_cast<int>(acquired.status));
            return;
        }

        WGPUTextureView view = wgpuTextureCreateView(acquired.texture, nullptr);
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(state.device, nullptr);

        // The dispatch runs first, in the same encoder: the pass reads what it wrote, so the
        // triangle's colour is this frame's computed value rather than a constant in the shader.
        WGPUComputePassDescriptor compute_pass_desc{};
        WGPUComputePassEncoder compute_pass = wgpuCommandEncoderBeginComputePass(encoder, &compute_pass_desc);
        wgpuComputePassEncoderSetPipeline(compute_pass, state.compute_pipeline);
        wgpuComputePassEncoderSetBindGroup(compute_pass, 0, state.bind_group, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(compute_pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(compute_pass);

        WGPURenderPassColorAttachment color{};
        color.view = view;
        color.loadOp = WGPULoadOp_Clear;
        color.storeOp = WGPUStoreOp_Store;
        // A deep blue: anything but the black a page that never presented shows.
        color.clearValue = WGPUColor{0.25, 0.45, 0.85, 1.0};
        // A 2D attachment names no slice. Zero is a slice, and the validator says so.
        color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

        WGPURenderPassDescriptor pass_desc{};
        pass_desc.colorAttachmentCount = 1;
        pass_desc.colorAttachments = &color;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
        wgpuRenderPassEncoderSetPipeline(pass, state.triangle_pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, state.bind_group, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);

        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(state.queue, 1, &commands);

#if !defined(__EMSCRIPTEN__)
        wgpuSurfacePresent(state.surface);
#endif
        // The window contents are delivered by the event loop, not by the present call alone.
        wgpuInstanceProcessEvents(state.instance);

        if (++state.frames == 1) {
            SDL_Log("webgpu_clear: first frame presented");
        }
        if (state.frames >= FRAME_LIMIT) {
            SDL_Log("webgpu_clear: %llu frames presented, done", static_cast<unsigned long long>(state.frames));
#if defined(__EMSCRIPTEN__)
            emscripten_cancel_main_loop();
#else
            state.ready = false;
#endif
        }
    }
} // namespace

int main(int, char **) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("webgpu_clear: SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("WebGPU clear", WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        SDL_Log("webgpu_clear: window creation failed: %s", SDL_GetError());
        return 1;
    }

    static State state{};
    state.instance = wgpuCreateInstance(nullptr);
    if (state.instance == nullptr) {
        SDL_Log("webgpu_clear: no instance");
        return 1;
    }

#if defined(__EMSCRIPTEN__)
    // The canvas the shell page provides. Emscripten's SDL builds it with this id.
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas_source{};
    canvas_source.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    const char *selector = "#canvas";
    canvas_source.selector.data = selector;
    canvas_source.selector.length = SDL_strlen(selector);

    WGPUSurfaceDescriptor surface_desc{};
    surface_desc.nextInChain = &canvas_source.chain;
#else
    // A desktop surface names the window SDL created.
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    void *hwnd = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (hwnd == nullptr) {
        SDL_Log("webgpu_clear: the window has no HWND");
        return 1;
    }

    WGPUSurfaceSourceWindowsHWND hwnd_source{};
    hwnd_source.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
    hwnd_source.hinstance = GetModuleHandle(nullptr);
    hwnd_source.hwnd = hwnd;

    WGPUSurfaceDescriptor surface_desc{};
    surface_desc.nextInChain = &hwnd_source.chain;
#endif
    state.surface = wgpuInstanceCreateSurface(state.instance, &surface_desc);
    if (state.surface == nullptr) {
        SDL_Log("webgpu_clear: no surface");
        return 1;
    }
    SDL_Log("webgpu_clear: surface created");

    WGPURequestAdapterOptions adapter_options{};
    adapter_options.compatibleSurface = state.surface;
    adapter_options.powerPreference = WGPUPowerPreference_HighPerformance;

    WGPURequestAdapterCallbackInfo adapter_cb{};
    adapter_cb.mode = WGPUCallbackMode_AllowProcessEvents;
    adapter_cb.callback = on_adapter;
    adapter_cb.userdata1 = &state;
    wgpuInstanceRequestAdapter(state.instance, &adapter_options, adapter_cb);

#if defined(__EMSCRIPTEN__)
    // The adapter callback asks for the device and the device callback starts rendering; nothing is
    // waited for here, because the page answers between animation frames and a loop in `main` would
    // hold the turn that answers it.
    SDL_Log("webgpu_clear: requested; the page drives the frames");
    // A frame loop is installed before the device arrives: a page whose main returns has nothing
    // left to run the callbacks the requests queued, and the loop's body does nothing until the
    // device callback marks the state ready.
    emscripten_set_main_loop_arg(OnAnimationFrame, &state, 0, true);
#else
    Pump(state, "requestDevice");
    if (state.adapter == nullptr) {
        SDL_Log("webgpu_clear: no adapter");
        return 1;
    }
    if (state.device == nullptr) {
        SDL_Log("webgpu_clear: no device");
        return 1;
    }
    if (state.failed) {
        return 1;
    }

    while (state.frames < FRAME_LIMIT) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                state.frames = FRAME_LIMIT;
            }
        }
        RenderFrame(state);
        SDL_Delay(16);
    }
#endif

    return 0;
}