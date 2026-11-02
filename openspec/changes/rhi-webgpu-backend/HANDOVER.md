# Handover

Written at the end of a long session, for whoever picks this up next. It says what is real, what
is not, and what not to spend time on. Everything here is verified against the tree at `33bf07e8`.

**Read this before the other documents.** `STATUS.md` is the longer record of how the change got
here; `tasks.md` tracks the 51 tasks; this file is the shortest accurate description of the state.

---

## Where it stands in one sentence

The seam exists and is enforced, the WebGPU backend exists and **presents a frame on both desktop
and in a browser through a standalone example**, and the **engine's own frame loop still does not
draw** - it now gets as far as encoding a command buffer and is rejected there.

---

## What is real

### Two captured frames

| Evidence | What it proves |
|---|---|
| `evidence/desktop-webgpu-frame.png` | a desktop window cleared through a WebGPU Win32 surface |
| `evidence/browser-webgpu-frame.png` | a browser canvas cleared through the same code compiled to wasm |

`example/webgpu_clear_example` is that code. It does **not** use the RHI, deliberately: it answers
whether a surface can be acquired and presented at all, and it says yes on both targets. It is a
platform probe, not a renderer.

### The backend

`engine/Rhi/Backend/WebGPU/` - 16 units, 31 objects, links into all three builds. It runs the
engine's own device and context creation, creates a surface from the window, configures it from its
capabilities, creates the frame manager's semaphores, and builds camera and scene descriptor sets.

### The build

| Preset | State |
|---|---|
| `msvc-debug` | builds, **76 / 78 tests** (the two failures are environmental - see below) |
| `emscripten` | builds and links; the example page runs in a browser |
| `webgpu-desktop` | builds; the clear example presents |

### The render graph builder compiles on both backends

`RenderGraphBuilder` was a stub for a long time. It now compiles for WebGPU by guarding four small
regions rather than one large one, and `ComplexRenderGraphBuilder::BuildDefaultRenderGraph` runs.

---

## What is not real

### The engine does not draw a frame

The browser example runs the engine's initialization, loads its baked shaders, creates its textures
and descriptor sets - and then hands WebGPU a command buffer that fails validation. The message at
the end of the session was:

```
The layer count (4) of [TextureView of Texture "Default shadow map"] used as attachment is greater than 1.
... [Invalid CommandBuffer] is invalid due to a previous error.
```

A correction for exactly that was written and built (`33bf07e8`) but **not verified in a browser**
before the session ended. Start there: re-run the example and read the first uncaptured error.

### No graphical test runs on WebGPU

The only test exercising this backend is `webgpu_device_test` (device creation, a buffer, a
texture, layout caching). Nothing draws.

---

## Environment failure that looks like a regression

`data_root_test` and `asset_path_test` fail with:

```
create_directories(...\needrename_data_root_test\override): Access is denied.  (error 5)
```

Established by experiment:

- making the directory by hand first makes the checks pass
- a standalone `std::filesystem` probe built with the same compiler creates that exact path
- it reproduces on a clean tree, and `git status` is empty there
- renaming the executable changes nothing

So it is the process being refused, not the code. **Do not chase it.**

---

## What to do next, in order

1. **Re-run the browser example and read the first uncaptured error.** Everything below the first
   one is a consequence. The loop is: run, read the sentence the validator wrote, fix that
   sentence, repeat. This has been the only method that works.

2. **Whatever that error is, it is probably the attachment/view or the pipeline.** The engine's
   clear example already proves present works; what fails now is command encoding, so the fault is
   in bind groups, pipeline layouts or render pass attachments.

3. **Then the desktop windowed path.** `RenderSystem` picks `SurfacePresentProvider` for a windowed
   run on this backend and it constructs, but no windowed example has been run to a frame yet.

4. **Then the graphical tests.** `mrt_test` and `skybox_test` are the smallest that exercise
   windowed rendering on Vulkan; they are the natural first two to port.

---

## What not to redo

Each of these cost real time and is settled.

| Do not | Why |
|---|---|
| Call `wgpuBufferGetMapState` | wgpu-native panics with `unimplemented!()`. Decide mapped-ness from `BufferTypeBits` instead. |
| Pass `maxAnisotropy 0` to `wgpuDeviceCreateSampler` | Rejected; clamp to 1. |
| Expect `imgui_impl_wgpu.cpp` to compile against wgpu-native 29 | It targets an older header. `GUISystem` is a stub here on purpose. |
| Use `MapRead` with `Uniform`/`Storage` | Only `CopyDst` may accompany `MapRead`. And `MapWrite` only with `CopySrc`. |
| Give ktx `vkFormat = 0` | ktx identifies textures by the specification's Vulkan format number. `Rhi::GetVkFormatValue` answers it; read the values from `vulkan_core.h`, do not recall them. |
| Guard `RenderGraphBuilder` as one region | It interleaves ordering, the cross-queue merge and the compiled passes. Four small guards work; one large one does not. Details in `STATUS.md`. |
| Assume `--screenshot` will capture a page with a frame loop | It decides for itself when a page is finished. Use `tools/dsh/cdp_shot.mjs`, which asks for the capture when it chooses and returns the page's console and exceptions. |
| Judge a frame by whether the canvas is black | The first clear colour was a deep blue that reads as black. A mid blue makes the result visible; an example whose result cannot be seen is not evidence. |
| Build the wasm engine as side modules | Emscripten side modules killed the first allocation with "null function". The engine is one module; `BUILD_SHARED_LIBS` answers the library type and `bin/` must hold no stale `.so`. |
| Expect `SDL_CreateWindow` to succeed with `SDL_WINDOW_VULKAN` on this backend | It fails with a message that only appears if you add `SDL_GetError()` - which is why that was added. |
| Spin on `requestAdapter` without Asyncify | The browser answers on the event loop the caller is occupying. `-sASYNCIFY` plus `emscripten_sleep` in the wait is what makes it resolve. |
| Assume `wgpuSurfacePresent` works on a canvas | It aborts: "use requestAnimationFrame via html5.h instead". The windowed path still presents. |
| Configure a surface with an assumed format | Read `wgpuSurfaceGetCapabilities` first; a canvas and a window offer different lists and configuring with the wrong one asserts. |

---

## The build, exactly

```sh
cmake --preset msvc
cmake --build --preset msvc-debug
ctest --preset msvc-debug                     # 76/78; the two are environmental

cmake --preset emscripten
cmake --build build/emscripten --config Debug --target external_resource_loading_example

cmake --preset webgpu-desktop
cmake --build build/webgpu --config Debug --target webgpu_clear_example
```

To see a browser frame: serve `build/emscripten/bin` over HTTP (the module loader needs `fetch`,
which `file://` blocks), start Edge with `--remote-debugging-port=9222 --enable-unsafe-webgpu`, then

```sh
node tools/dsh/cdp_shot.mjs http://127.0.0.1:9222 http://127.0.0.1:8931/<page>.html out.png 90000 1000 760
```

**Baking the built-in shaders** is a separate step the Emscripten configure skips (a cross build
cannot run a baker built for the target):

```sh
build/msvc/bin/Debug/shader_baker.exe build/emscripten/stage/Debug/builtin_assets/shaders
```

Run it after the native build exists. The example preloads that staged tree.

---

## Machine setup this expects

| Tool | Where it was |
|---|---|
| MSVC / VS 2026 | `C:\Program Files\Microsoft Visual Studio\18\Community` |
| Vulkan SDK 1.4.313.2 | `C:\VulkanSDK\1.4.313.2` |
| SDL3 3.4.16 | `C:\SDL3_SDK\SDL3-3.4.16` |
| wgpu-native | `D:\tools\wgpu-native` (`include/webgpu/webgpu.h`, `lib/wgpu_native.dll`) |
| Tint | `D:\tools\dawn\...\bin\tint.exe` |
| emsdk | `C:\emsdk` |
| Python | `.venv` in the repo root |

---

## The honest summary

**Two frames by a probe; no frame by the engine.** The probe proves the surface path on both
targets, which was the largest unknown a few rounds ago. The engine's own path is one validation
error away from encoding a command buffer, and that error has been narrowed three times this
session (`bindingArraySize`, combined-sampler entries, single-layer views).

The remaining work is not a design problem any more. It is the same loop that produced every fix
listed above: run it, read the one sentence the validator wrote, and fix that sentence.
