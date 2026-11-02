# Handover

Written at the end of a session that took the engine's own path from "cannot encode" to
"encodes, submits and presents". It says what is real, what is not, and what to do next.

**Read this before the other documents.** `STATUS.md` is the longer record of how the change
got here; `tasks.md` tracks the 51 tasks; this file is the shortest accurate description of the
state.

Verified at `a0ca69fe` (branch `feature/wgpu`).

---

## Where it stands in one sentence

The engine's own scene draws and presents through the WebGPU backend on desktop - the full
windowed example runs to its last frame and the surface image it presents is the frame it
rendered - the skybox test's WebGPU frame is byte-for-byte the frame Vulkan produces, and the
browser build runs to its first frame before a wasm heap bug stops it.

---

## What is real, and how it was checked

| Evidence | What it proves |
|---|---|
| `msvc-debug`: build exit 0, 76/76 tests pass | the Vulkan backend is unchanged (`asset_path_test` and `data_root_test` are the two known environment failures, excluded here) |
| `webgpu-desktop`: build exit 0 | the whole engine compiles and links against the second backend |
| `external_resource_loading_example` on WebGPU: **exit 0**, 30 frames, frame captured | shadow cascades, the lit pass, the bloom compute pass and the present copy all run in the engine's own render graph |
| `skybox_test` on WebGPU and on Vulkan: both PASS, and the two dumps are **the same bytes** | the second backend draws this scene identically, not merely plausibly |
| `mrt_test` on WebGPU: PASSED | geometry with four render passes, plus a readback |
| `build/webgpu/scene3.png`, `build/webgpu/skybox2.png`, `build/skybox_vulkan.png`, `build/webgpu/presented.png` | the frames themselves |
| `build/emscripten`: build exit 0, page reaches "Entering main loop" in Edge | the browser build compiles, links, creates its device and surface, loads the model and starts a frame |
### How to look at a frame

```powershell
$env:NEEDRENAME_DATA_ROOT = (Resolve-Path build\webgpu\stage\Debug).Path
$env:SKYBOX_DUMP = "C:\tmp\skybox.bmp"           # or MRT_DUMP for mrt_test
build\webgpu\bin\Debug\skybox_test.exe 10
$env:NEEDRENAME_PRESENT_CAPTURE = "C:\tmp\presented.bmp"   # any windowed run
build\webgpu\bin\Debug\mrt_test.exe 6
```

`SKYBOX_DUMP`/`MRT_DUMP` write what the render target holds; `NEEDRENAME_PRESENT_CAPTURE`
writes what the surface received, which is the stronger of the two.

The BMPs are written bottom-up; convert and flip them, or open them as they are, to see the
frame the window showed.

### How to run the browser build

```powershell
cmake --preset emscripten -DSHADER_WGSL_CONVERTER=D:/tools/dawn/<build>/bin/tint.exe
cmake --build build/emscripten
# serve build/emscripten/bin, then open external_resource_loading_example.html
.venv\Scripts\python.exe -m http.server 8099 --bind 127.0.0.1   # from that directory
```

A headless run that shows the page's own log:

```powershell
& "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" --headless=new `
  --enable-unsafe-webgpu --user-data-dir=<scratch> --window-size=1280,720 `
  --virtual-time-budget=120000 --enable-logging=stderr --v=0 `
  --screenshot=shot.png "http://127.0.0.1:8099/external_resource_loading_example.html" 2> edge.log
```

`edge.log` carries every `console.log` the page makes, which is where the engine's `SDL_Log`
output goes. `build/emscripten/bin/capture*.html` are wrappers that route stdout, stderr and
`console.error`/`console.warn` through `console.log`; they reproduce the abort and, so far,
capture nothing the shell does not.


---

## What made the difference this session, in order

1. **The build could not be produced from clean.** `RenderResourceHasher` had no `f` overload,
   which the Vulkan resource caches call on every flag set. An incremental build hid it: the
   translation unit kept an object file from before the header was last edited, so a partial
   build mixed objects compiled against two different `DescriptorBinding` layouts - which
   surfaced as an access violation in an RHI test rather than as a link error. Fixed, and the
   lesson is in the commit: **when a shared header changes shape, check that a clean build
   works.**
2. **The bake covers the built-in shaders**, not just the physics tree. `shader_baker` now
   writes the WGSL variant and the interface description into the same archive as the SPIR-V,
   so one asset set serves both backends. It pins each combined sampler's split slot with
   `--sampler-mapping` (the converter otherwise chooses, and has been seen to choose one past
   the set's highest binding), and a shader that would need a slot above nine fails the bake
   instead of binding nothing.
3. **Push constants became a uniform buffer in group 3.** The bake moves the block there, the
   pipeline layout appends the group (padding with empty layouts when a pipeline declares
   fewer sets), and the command encoder publishes one zero-filled 256-byte window per recorded
   block into a ring the queue orders against submission.
4. **The seam states what a texture binding holds** - sample type, view dimension, and where a
   combined binding's sampler lands - because the standard interface has neither a combined
   binding nor a Vulkan layout to derive the shape from.
5. **The frame encoder is rebuilt when a frame slot is reset.** The frame manager cached one
   encoder per slot for the process's lifetime; the second frame recorded into an encoder that
   had already been released, which the device reported as a panic.
6. **Five defects the first real draws exposed**, each of which only a frame could show: a
   cube map read as a cube array, a "no comparison" sampler created as a comparison sampler, a
   readback row that has to be 256-byte aligned, a pipeline layout group left unbound, and a
   constant window whose tail was reading the previous frame.

---

## What is not real, and the order to take it

### 0. Two headless paths still fail on WebGPU

Found at the end of the session, both on the headless (no window) path:

- `pbr_test` draws its four subpasses and then takes an access violation around the frame
  readback. The windowed readback works, so the difference is the headless present provider and
  the readback pump's completion path.
- `compute_shader_test` asserts in `ShaderAsset.cpp` because the asset it wants is not in the
  staged data root; that is a staging gap rather than a backend fault, and it hangs on the
  assertion dialog.

### 1. The scene example no longer stalls; the earlier note is kept for the record

`external_resource_loading_example` gets through its frame's CPU work, acquires the surface
image, and stops inside `physics->GPUStep(...)`: the solver's `EnsureShadersLoaded` loads its
kernels one at a time, each printed by name in a log build, and the run never comes back from
one of them. Kernel creation is `wgpuDeviceCreateComputePipeline` on a synchronous device call,
so what is left to find is which kernel and why - the last one reached was
`XPBD AccumFixedPos` (a 15 KB WGSL module). Two things to try, in this order:

- Log around `ComputeKernel::CreateFromModule` (module creation, set layouts, pipeline layout,
  pipeline) rather than around the loader, to see the last call that returns.
- Check whether it is slow rather than stuck: the browser's own web build compiles the same
  modules without this stall, so a build with `-O2`/`RelWithDebInfo` (where naga is not in
  debug) is the cheapest way to tell the two apart.

### 2. The browser aborts in the wasm heap during its first frame

The page runs: device, surface, model, camera, main loop, and the first frame's cascade fit. It
then aborts with

```
Aborted(Assertion failed: region_is_in_use(region), at: /emsdk/emscripten/system/lib/emmalloc.c,892,emmalloc_free)
```

which is emmalloc refusing to free a region it believes is in use: a double free, or a heap
write that corrupted emmalloc's bookkeeping. That is a memory bug in the wasm path, not a
missing WebGPU call, and nothing in the desktop build shows it.

The abort happens inside the first frame's recording: it prints after `BeginMainCommandBuffer`
returns and before `CompleteFrame`, so the work in between - the physics step, the model-matrix
compute and the render graph's passes - is where to look. Four experiments are already spent,
and recording them is cheaper than repeating them:

- `-sSAFE_HEAP=1` reports an alignment fault in the glTF loader instead, which is that loader
  doing unaligned reads on purpose; it says nothing about this bug.
- `-sASSERTIONS=2` prints no stack for this abort: the generated `abort()` prints the message
  only. The source line has to come from bisecting.
- `-sASYNCIFY_STACK_SIZE=2097152` (now in the example's link options) changes nothing, but the
  4 KB default was worth ruling out - Asyncify copies the stack of the frame that sleeps.
- `Module.ENV` does **not** reach `SDL_getenv` in the page, so an environment variable is not a
  bisect tool in the browser. `MainClass::RunOneFrame` grew `NEEDRENAME_SKIP_PHYSICS`,
  `NEEDRENAME_SKIP_MODEL_MATRICES` and `NEEDRENAME_SKIP_PASSES` for the desktop; the browser
  needs the same three as `Module.arguments` or a compile-time define.

The likeliest places remain the ones written for the wasm build and sized by hand:
`SubmissionHelper::EnqueueTextureBufferSubmission`'s padded row copy, `BuildClearData`,
`CommandEncoder`'s constant window, and the readback staging's row loop.

### 3. Physics on WebGPU

Kernels now build: the loader takes the baked module and description, the layouts state each
binding's access and format, the constant block's group is declared, and the device asks for
enough storage bindings per stage. Nothing has *dispatched* one yet on desktop - the example
stalls in kernel creation (item 1) - and the browser reaches the first frame's physics before
the assertion in item 2.

### 4. The windowed tests that still skip this backend

`mrt_test` and `skybox_test` were ported to the seam and now pass on WebGPU. `imgui_test` is
still Vulkan-only: the ImGui WebGPU back-end does not compile against wgpu-native 29, which is
task 10.4 and unchanged.

---

## Things not to redo

| Do not | Why |
|---|---|
| Chase a fault as a code regression before checking that a clean build works | a stale object file cost this session several hours; the symptom was an access violation at device creation |
| Expect the converter to place a combined sampler's split slot predictably | it picks per module; the bake pins the slot with `--sampler-mapping`, and the mapping's destination is a single digit |
| Bind a Vulkan-style partial set of groups | a draw must bind every group the pipeline layout declares, even unused ones |
| Compare a sampler's `DSComparator::Always` to "comparison enabled" | the seam uses `Always` for "no comparison" |
| Copy an image to a buffer with a tight row pitch | rows must start every 256 bytes; stage through a padded buffer |
| Use `{}` for a `WGPU*` structure with an `_INIT` macro | their defaults are not all zero (`WGPURenderPassColorAttachment::depthSlice` is the one that bites) |
| Call `wgpuBufferGetMapState` | wgpu-native panics with `unimplemented!()` |
| Pass `maxAnisotropy 0` | rejected; clamp to 1 |
| Expect `imgui_impl_wgpu.cpp` to compile against wgpu-native 29 | it targets an older header; `GUISystem` is a stub here on purpose |
| Judge a frame by whether the canvas is black | the clear colour and the scene colour have to be distinguishable; an example whose result cannot be seen is not evidence |

---

## The build, exactly

```sh
cmake --preset msvc
cmake --build --preset msvc-debug
ctest --preset msvc-debug -E "asset_path_test|data_root_test"

cmake --preset webgpu-desktop \
  -DSHADER_WGSL_CONVERTER=D:/tools/dawn/<dawn>/bin/tint.exe
cmake --build build/webgpu --config Debug
```

The WebGPU build runs against the **staged** data root, because that is where the baked shader
archives are:

```powershell
$env:NEEDRENAME_DATA_ROOT = (Resolve-Path build\webgpu\stage\Debug).Path
build\webgpu\bin\Debug\mrt_test.exe 3
```

`shader_baker` is a host tool: a Vulkan configure builds it, and a WebGPU or cross configure
finds it automatically at `build/msvc/bin/Debug/shader_baker.exe` (or by setting
`ENGINE_HOST_SHADER_BAKER`). A cross build cannot build one itself - it has no GLSL compiler.

---

## Machine setup this expects

| Tool | Where it was |
|---|---|
| MSVC / VS 2026 | `C:\Program Files\Microsoft Visual Studio\18\Community` |
| Vulkan SDK 1.4.313.2 | `C:\VulkanSDK\1.4.313.2` |
| SDL3 3.4.16 | `C:\SDL3_SDK\SDL3-3.4.16` |
| wgpu-native | `D:\tools\wgpu-native` |
| Tint | `D:\tools\dawn\Dawn-97ae1b10ac9d579f7e6b790ce7fc4faa1041f820-windows-latest-Release\bin\tint.exe` |
| emsdk | `C:\emsdk` |
| Python | `.venv` in the repo root |

`SHADER_WGSL_CONVERTER` is a cache variable, discovered when it is not set; on this machine it
has to be passed once because the Dawn build is not on `PATH`.