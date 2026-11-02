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
#include <webgpu/webgpu.h>

#if !defined(__EMSCRIPTEN__)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cstdio>
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
        // itself instead of hanging with no output.
        const uint64_t deadline = SDL_GetTicks() + 10000;
        while (!adapter_done || !device_done) {
            wgpuInstanceProcessEvents(state.instance);
            if (SDL_GetTicks() > deadline) {
                SDL_Log("webgpu_clear: %s did not resolve within 10s", what);
                return;
            }
            SDL_Delay(1);
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

    WGPUSurfaceConfiguration surface_config{};
    surface_config.device = state.device;
    surface_config.format = SURFACE_FORMAT;
    surface_config.usage = WGPUTextureUsage_RenderAttachment;
    surface_config.width = state.width;
    surface_config.height = state.height;
    surface_config.presentMode = WGPUPresentMode_Fifo;
    surface_config.alphaMode = WGPUCompositeAlphaMode_Auto;
    wgpuSurfaceConfigure(state.surface, &surface_config);
    SDL_Log("webgpu_clear: surface configured %ux%u", state.width, state.height);

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
        wgpuSurfaceGetCurrentTexture(state.surface, &acquired);
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
        color.clearValue = WGPUColor{0.05, 0.15, 0.45, 1.0};
        // A 2D attachment names no slice. Zero is a slice, and the validator says so.
        color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

        WGPURenderPassDescriptor pass_desc{};
        pass_desc.colorAttachmentCount = 1;
        pass_desc.colorAttachments = &color;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
        wgpuRenderPassEncoderEnd(pass);

        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(state.queue, 1, &commands);

        wgpuSurfacePresent(state.surface);
        // The window contents are delivered by the event loop, not by the present call alone.
        wgpuInstanceProcessEvents(state.instance);


        if (++frames == 1) {
            SDL_Log("webgpu_clear: first frame presented");
        }
        // A fixed number of frames so a headless run finishes; a desktop run closes its window.
        if (frames >= 600) {
            SDL_Log("webgpu_clear: %llu frames presented, done", static_cast<unsigned long long>(frames));
            running = false;
        }
        SDL_Delay(16);
    }

    return 0;
}






