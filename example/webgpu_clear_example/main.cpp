// A browser page that clears its canvas to a colour through WebGPU.
//
// This is the smallest thing that proves the surface path: an instance, a surface built from the
// canvas the shell provides, a configuration, and one pass per frame that clears the acquired
// texture. It does not use the RHI, and that is the point - it answers whether a canvas can be
// presented to at all on this target before the engine's frame loop depends on the answer.
//
// The colour is deliberately not black: a black canvas is what a page that never presented looks
// like, and the whole question here is whether anything reaches the screen.

#include <SDL3/SDL.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif
#include <webgpu/webgpu.h>

#if !defined(__EMSCRIPTEN__)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace {
    constexpr uint32_t WINDOW_WIDTH = 800;
    constexpr uint32_t WINDOW_HEIGHT = 600;
    constexpr WGPUTextureFormat SURFACE_FORMAT = WGPUTextureFormat_BGRA8Unorm;

    struct State {
        WGPUInstance instance{nullptr};
        WGPUSurface surface{nullptr};
        WGPUAdapter adapter{nullptr};
        WGPUDevice device{nullptr};
        WGPUQueue queue{nullptr};
        uint32_t width{WINDOW_WIDTH};
        uint32_t height{WINDOW_HEIGHT};
    };

    // The triangle and the dispatch this example exists to prove, as one source string each. Both
    // read and write the same storage buffer, so the colour on the screen is the value the compute
    // stage wrote: a dispatch that never ran leaves a black triangle, not a differently-coloured one.
    constexpr const char *WGSL = R"(
struct Colour { value : vec4f };
@group(0) @binding(0) var<storage, read_write> computed : Colour;

@compute @workgroup_size(1)
fn main() {
    computed.value = vec4f(0.95, 0.55, 0.15, 1.0);
}
)";

    constexpr const char *TRIANGLE_WGSL = R"(
struct Colour { value : vec4f };
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
    bool adapter_done = false;
    bool device_done = false;

    void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView, void *userdata, void *) {
        SDL_Log("webgpu_clear: adapter status %d", static_cast<int>(status));
        if (status == WGPURequestAdapterStatus_Success) {
            static_cast<State *>(userdata)->adapter = adapter;
        }
        adapter_done = true;
    }

    void on_device(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView, void *userdata, void *) {
        SDL_Log("webgpu_clear: device status %d", static_cast<int>(status));
        if (status == WGPURequestDeviceStatus_Success) {
            static_cast<State *>(userdata)->device = device;
        }
        device_done = true;
    }

    void Pump(State &state, const char *what) {
        // Adapter and device requests resolve on the implementation's schedule; processEvents is
        // what lets the callbacks run. The bound is here so a request that never resolves reports
        // A deadline in *iterations*, not in milliseconds: under a browser's virtual clock the
        // wall clock jumps past any deadline before the event loop has had a turn, which is how
        // this loop first reported "did not resolve" while the device was about to arrive.
        int iterations = 0;
        while (!adapter_done || !device_done) {
            wgpuInstanceProcessEvents(state.instance);
            if (++iterations > 2000) {
                SDL_Log("webgpu_clear: %s did not resolve", what);
                return;
            }
            // A browser answers the adapter and device promises on its own event loop, so the
            // wait has to hand control back to it. SDL_Delay does not imply that under Asyncify;
            // mscripten_sleep does.
#if defined(__EMSCRIPTEN__)
            emscripten_sleep(1);
#else
            SDL_Delay(1);
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

    State state{};
    state.instance = wgpuCreateInstance(nullptr);
    if (state.instance == nullptr) {
        SDL_Log("webgpu_clear: no instance");
        return 1;
    }

#if defined(__EMSCRIPTEN__)
    // The canvas the shell page provides. Emscripten's SDL builds it with this id.
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas_source{};
    canvas_source.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvas_source.selector = WGPU_STRING_VIEW_INIT;
    const char *selector = "#canvas";
    canvas_source.selector.data = selector;
    canvas_source.selector.length = SDL_strlen(selector);

    WGPUSurfaceDescriptor surface_desc{};
    surface_desc.nextInChain = &canvas_source.chain;
#else
    // A desktop surface names the window SDL created. The instance handle is the module's, which
    // is what the wrapper asks for.
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
    Pump(state, "requestAdapter/requestDevice");

    if (state.adapter == nullptr) {
        SDL_Log("webgpu_clear: no adapter");
        return 1;
    }

    WGPUDeviceDescriptor device_desc{};
    WGPURequestDeviceCallbackInfo device_cb{};
    device_cb.mode = WGPUCallbackMode_AllowProcessEvents;
    device_cb.callback = on_device;
    device_cb.userdata1 = &state;
    wgpuAdapterRequestDevice(state.adapter, &device_desc, device_cb);
    Pump(state, "requestAdapter/requestDevice");

    if (state.device == nullptr) {
        SDL_Log("webgpu_clear: no device");
        return 1;
    }
    state.queue = wgpuDeviceGetQueue(state.device);

    // Ask the surface what it can do instead of assuming a format. A canvas and a window do
    // not offer the same list, and configure asserts on a format it does not have.
    WGPUSurfaceCapabilities capabilities{};
    wgpuSurfaceGetCapabilities(state.surface, state.adapter, &capabilities);
    SDL_Log("webgpu_clear: %zu formats, %zu present modes, %zu alpha modes", capabilities.formatCount, capabilities.presentModeCount, capabilities.alphaModeCount);
    for (size_t i = 0; i < capabilities.formatCount && i < 6; ++i) {
        SDL_Log("webgpu_clear:   format %d", static_cast<int>(capabilities.formats[i]));
    }
    WGPUTextureFormat chosen_format = SURFACE_FORMAT;
    if (capabilities.formatCount > 0) {
        chosen_format = capabilities.formats[0];
    }
    SDL_Log("webgpu_clear: using format %d", static_cast<int>(chosen_format));
    WGPUPresentMode chosen_present = WGPUPresentMode_Fifo;
    if (capabilities.presentModeCount > 0) {
        chosen_present = capabilities.presentModes[0];
    }
    WGPUCompositeAlphaMode chosen_alpha = WGPUCompositeAlphaMode_Opaque;
    if (capabilities.alphaModeCount > 0) {
        chosen_alpha = capabilities.alphaModes[0];
    }
    SDL_Log("webgpu_clear: using present mode %d, alpha mode %d", static_cast<int>(chosen_present), static_cast<int>(chosen_alpha));
    WGPUSurfaceConfiguration surface_config{};
    surface_config.device = state.device;
    surface_config.format = chosen_format;
    surface_config.usage = WGPUTextureUsage_RenderAttachment;
    surface_config.width = state.width;
    surface_config.height = state.height;
    // Fifo waits for a vertical blank, and a headless browser has none to give: the acquire
    // would block. Immediate is what a page that only needs to prove it presented wants.
    surface_config.presentMode = chosen_present;
    // Opaque, not Auto: an auto alpha lets the compositor decide, and a window whose
    // contents are blended with nothing underneath is exactly where a black result comes from.
    surface_config.alphaMode = chosen_alpha;
    wgpuSurfaceConfigure(state.surface, &surface_config);
    SDL_Log("webgpu_clear: surface configured %ux%u", state.width, state.height);

    // A storage buffer, a compute pipeline that writes it, and a triangle pipeline that reads it.
    // The buffer needs the storage usage once and both stages read it; the dispatch runs in the
    // same command encoder as the pass, so the frame's colour comes from this frame's dispatch.
    const uint64_t colour_bytes = 16;
    WGPUBufferDescriptor buffer_desc{};
    buffer_desc.size = colour_bytes;
    buffer_desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    WGPUBuffer colour_buffer = wgpuDeviceCreateBuffer(state.device, &buffer_desc);


    WGPUShaderSourceWGSL compute_source{};
    compute_source.chain.sType = WGPUSType_ShaderSourceWGSL;
    compute_source.code = WGPUStringView{WGSL, SDL_strlen(WGSL)};
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
    bind_entry.buffer = colour_buffer;
    bind_entry.offset = 0;
    bind_entry.size = colour_bytes;
    WGPUBindGroupDescriptor bind_group_desc{};
    bind_group_desc.layout = bind_group_layout;
    bind_group_desc.entryCount = 1;
    bind_group_desc.entries = &bind_entry;
    WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(state.device, &bind_group_desc);

    WGPUComputePipelineDescriptor compute_pipeline_desc{};
    compute_pipeline_desc.layout = pipeline_layout;
    compute_pipeline_desc.compute.module = compute_module;
    compute_pipeline_desc.compute.entryPoint = WGPUStringView{"main", 4};
    WGPUComputePipeline compute_pipeline = wgpuDeviceCreateComputePipeline(state.device, &compute_pipeline_desc);

    WGPUMultisampleState multisample{};
    multisample.count = 1;
    multisample.mask = 0xFFFFFFFFu;

    WGPURenderPipelineDescriptor render_pipeline_desc{};
    render_pipeline_desc.layout = pipeline_layout;
    render_pipeline_desc.vertex.module = triangle_module;
    render_pipeline_desc.vertex.entryPoint = WGPUStringView{"vs_main", 7};
    WGPUColorTargetState target{};
    target.format = chosen_format;
    // A multisample count is required: zero is rejected, and one is the default single-sample path.
    target.nextInChain = nullptr;
    target.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fragment{};
    fragment.module = triangle_module;
    fragment.entryPoint = WGPUStringView{"fs_main", 7};
    fragment.targetCount = 1;
    fragment.targets = &target;
    render_pipeline_desc.fragment = &fragment;
    render_pipeline_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    render_pipeline_desc.multisample = multisample;
    WGPURenderPipeline triangle_pipeline = wgpuDeviceCreateRenderPipeline(state.device, &render_pipeline_desc);
    if (compute_pipeline == nullptr || triangle_pipeline == nullptr) {
        SDL_Log("webgpu_clear: pipeline creation failed");
        return 1;
    }
    SDL_Log("webgpu_clear: compute and triangle pipelines created");
    bool running = true;
    uint64_t frames = 0;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }

        WGPUSurfaceTexture acquired{};
        static int logged = 0;
        wgpuSurfaceGetCurrentTexture(state.surface, &acquired);
        if (logged < 3) {
            SDL_Log("webgpu_clear: acquire status %d texture %p", static_cast<int>(acquired.status), (void*)acquired.texture);
            ++logged;
        }
        if (acquired.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal
            && acquired.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
            SDL_Log("webgpu_clear: acquire status %d", static_cast<int>(acquired.status));
            continue;
        }

        WGPUTextureView view = wgpuTextureCreateView(acquired.texture, nullptr);

        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(state.device, nullptr);
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

        // The dispatch runs first, in the same encoder: the pass reads what it wrote, so the
        // triangle's colour is this frame's computed value and not a constant in the shader.
        WGPUComputePassDescriptor compute_pass_desc{};
        WGPUComputePassEncoder compute_pass = wgpuCommandEncoderBeginComputePass(encoder, &compute_pass_desc);
        wgpuComputePassEncoderSetPipeline(compute_pass, compute_pipeline);
        wgpuComputePassEncoderSetBindGroup(compute_pass, 0, bind_group, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(compute_pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(compute_pass);

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
        wgpuRenderPassEncoderSetPipeline(pass, triangle_pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);

        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(state.queue, 1, &commands);

#if defined(__EMSCRIPTEN__)
        // A canvas is composited by the page on its next animation frame; there is no present
        // call to make, and the port rejects one that is.
#else
        wgpuSurfacePresent(state.surface);
#endif
        // The window contents are delivered by the event loop, not by the present call alone.
        wgpuInstanceProcessEvents(state.instance);


        if (++frames == 1) {
            SDL_Log("webgpu_clear: first frame presented");
        }
        // A fixed number of frames so a headless run finishes; a desktop run closes its window.
        if (frames >= 30) {
            SDL_Log("webgpu_clear: %llu frames presented, done", static_cast<unsigned long long>(frames));
            running = false;
        }
        SDL_Delay(16);
    }

    return 0;
}


