# Handover

Written at the end of a session that took the engine's own path from "cannot encode" to
"encodes, submits and presents". It says what is real, what is not, and what to do next.

**Read this before the other documents.** `STATUS.md` is the longer record of how the change
got here; `tasks.md` tracks the 51 tasks; this file is the shortest accurate description of the
state.

Verified at `a0ca69fe` (branch `feature/wgpu`).

---

## Where it stands in one sentence

The engine now builds a scene, records it, submits it and presents it through the WebGPU
backend **without a validation error**, on desktop; **the frame is still black**, and the
browser build has not been run since the bake work landed.

---

## What is real, and how it was checked

| Evidence | What it proves |
|---|---|
| `msvc-debug`: build exit 0, 54 tests pass | the Vulkan backend is unchanged (`asset_path_test` and `data_root_test` are the two known environment failures, excluded here) |
| `webgpu-desktop`: build exit 0 | the whole engine compiles and links against the second backend |
| `mrt_test`, `skybox_test` on WebGPU | the engine creates its device, surface, descriptor sets and material pipelines, records four render passes, submits them and presents - with **no uncaptured validation error and no panic** |

The two tests still **fail**, with their own assertions:

- `mrt_test`: "the read-back colour target is uniformly zero".
- `skybox_test`: the frame is black.

So: the plumbing is proven end to end; **the picture is not**.

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

### 1. The frame is black (the first thing to fix)

Both windowed runs present a black frame with no validation error, so the fault is in what the
shaders compute or in what reaches the surface. The fastest way to separate the two:

- `mrt_test` reads its colour target back through `RecordCopyImageToBuffer`. That copy now
  completes - the row-alignment staging works - and the pump now fires, so the callback runs.
  **But the map of the staging buffer fails**: `wgpuBufferMapAsync` answers
  `WGPUMapAsyncStatus_Error` with only "Validation Error" as its message, so the callback
  receives a buffer whose `GetVMAddress` is null and the test faults on it.
  - The buffer is created as `CopyTo | HostRandomAccess`, which the allocator turns into
    `MapRead | CopyDst`; the map is requested with `WGPU_WHOLE_MAP_SIZE` after the work is done
    and outside the device's event dispatch (`BufferAllocation::PrepareReadback`). What is left
    to check is the buffer *this* map names: the readback registry's staging buffer, not the
    caller's. Printing `wgpuBufferGetSize` and the descriptor's usage at creation, or turning on
    wgpu-native's log level (`wgpuSetLogLevel`, in `wgpu.h`), is the next step - the summary
    status carries no detail.
- If the readback bytes are non-zero, the draw is fine and the fault is in the present path:
  `SurfacePresentProvider` on this backend, which copies the final colour target into the
  surface texture and presents it. That copy is the other half of the same question and has
  never been observed producing a picture.

### 2. The browser build has not been run since the bake work

The Emscripten preset still points at a host baker through `ENGINE_HOST_SHADER_BAKER`, and the
staged tree now carries WGSL for every built-in shader, so a browser run is the next thing to
attempt after the desktop frame. Two things are expected to block it:

- **The render path uses runtime reflection** (`SPLayout::Reflect`) to build material layouts
  and to place material uniform variables. The browser build has no SPIRV-Cross by design
  (task 11.2), so either that reflection has to be replaced by the baked interface description
  - which needs variable offsets, the part of task 7.1 that is still missing - or SPIRV-Cross
  has to be linked into the wasm module and task 11.2 recorded as not met.
- **`RequestComputeKernel` throws** on this backend by design; the physics loader now takes the
  baked-module path (`PhysicsSpirvLoader`), which the browser build can use as soon as its
  physics artifacts are staged.

### 3. Physics on WebGPU

`PhysicsSpirvLoader` now loads `<module>.wgsl` and `<module>.iface.json` and creates the kernel
through `RequestComputeKernelFromModule`, and the desktop build implements that. It has not been
run: the render graph's compute passes fail first on a storage-image format mismatch
(`bloom.comp` wants an `Rg11b10Ufloat` read-only storage texture, the seam's binding description
carries no format). The honest fix is to record the storage image's format in
`DescriptorBinding` from the reflection, which is a small addition.

### 4. The two windowed tests that used to skip this backend

`mrt_test` and `skybox_test` were ported to the seam this session (`RecordCopyImageToBuffer`
instead of a raw Vulkan copy). `imgui_test` is still Vulkan-only: the ImGui WebGPU back-end does
not compile against wgpu-native 29, which is task 10.4 and unchanged.

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
