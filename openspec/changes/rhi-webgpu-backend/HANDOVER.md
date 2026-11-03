# Handover
has to be passed once because the Dawn build is not on `PATH`.

## Where it stands in one sentence

The engine's own scene draws and presents through the WebGPU backend on desktop - the full
windowed example runs to its last frame and the surface image it presents is the frame it
rendered - the skybox test's WebGPU frame is byte-for-byte the frame Vulkan produces, and the
browser page draws the same scene into its canvas with the material path reading the bake's description where a build has no reflection library.

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

### 0a. The example's black frame was a silently skipped kernel, and it is fixed

A dispatch consults an `available` flag that a device refusal sets to false. That flag was
initialised false and only ever set by `CreateFromModule`, so a kernel reaching a dispatch by any
other path was treated as refused and its work was skipped - the bloom pass and the model-matrix
compute became no-ops and the window went black with no error anywhere. The default is true now,
and a refusal still lands on the no-op branch because creation sets the flag explicitly.

Found by bisecting with a build-and-measure script (`build/bisect_run.ps1`): the frame is good up
to `ee689e6b`, black from `8f32eca7`, and reverting only `ComputeKernel.cpp` restored it. The
example presents a real frame again - 404 of a 40x23 sample grid lit, brightest pixel 161 - and
the capture can read the render target instead of the surface
(`NEEDRENAME_CAPTURE_SOURCE=target`) when the two have to be compared.

### 0. The example's composition has no Vulkan reference yet

The example's frame is stable and it draws its scene, but its framing looks wrong in a way the
two windowed tests cannot confirm or deny: the skybox's ground half sits at the top of the
image, which is what an inverted view-projection would produce, while `skybox_test` - same
shader, same camera class - produces a frame byte-identical to Vulkan's. The way to settle it is
to capture the Vulkan example the way the WebGPU one is captured: record a copy of the final
colour target into a host-visible buffer inside the Vulkan `SwapchainPresentProvider` and write
it out under the same `NEEDRENAME_PRESENT_CAPTURE`. Until then the difference is a question, not
a conclusion.

### 0d. The suite, measured on both backends

`ctest --preset msvc-debug -E "asset_path_test|data_root_test"` (Vulkan): **76 of 76 passed.**
`ctest --test-dir build/webgpu -C Debug` with `NEEDRENAME_DATA_ROOT` pointing at its stage:
**43 of 46 passed**, after `complex_mesh_test`, `pbr_test` and `shadow_constants_test` were fixed. The three that remain are the read-back stall below (`compute_shader_test`, `mrt_test`, `mrt_test_staged_builtin_assets`). The earlier six were:

- `shadow_constants_test` reads `interface.glsl` and `shadow.glsl` from the data root, and the
  WebGPU staging stores shaders in baked form, so there is no `.glsl` there to read. The Vulkan
  stage keeps them (12 files). An environment difference, not a backend one.
- `complex_mesh_test` and `pbr_test` segfault; `compute_shader_test`, `mrt_test` and
  `mrt_test_staged_builtin_assets` time out in the readback (0c).

Without `NEEDRENAME_DATA_ROOT` the same suite reports 36 of 46, because the windowed tests then
wait for assets that are not where the process looks - worth knowing before a number is believed.

`skybox_test` passes three times in a row standalone on WebGPU (640000/640000 pixels not black)
while timing out inside the suite, which is the readback/serialisation problem below rather than
a rendering one.

### 0c. `mrt_test` hangs allocating its read-back buffer, on both backends

Instrumented: the frame is recorded, `RegisterReadbackCallback` opens its registry, and then the
process stops inside the **first `vmaCreateBuffer`** - an 8,294,400-byte buffer of type
`ReadbackFromDevice`, which is `CopyTo | HostRandomAccess`, mapped on creation. Nothing after that
call is reached, on the Vulkan build and the WebGPU build alike, so it is the shared read-back
allocator rather than either backend.

Two things make it plausible and both are already known in the code: the test registers a read-back
**every frame**, each one a fresh 8 MB allocation (the registry carries the comment "TODO: reuse
fences and command buffers to avoid frequent reallocation"), and a callback only runs once the
previous frame's fence has signalled, so allocations outrun the frees under memory pressure. The
driver then sits in `vkAllocateMemory`, which is where the process is observed to be.

The first half is in: `RegisterReadbackCallback` borrows a staging buffer from a pool keyed by
size and returns it once the callback has run, and the callback takes it by reference instead of
owning it - it used to be handed a `unique_ptr` and had to destroy the very thing that should have
been reused. A read-back therefore costs no allocation after the first few frames.

That does not cure the hang, and the instrumentation that narrowed it further says the hang is
**not** in the read-back: registration completes ("staging ready", "copy recorded") and nothing
after it runs - no submit is ever reached. A windowed `skybox_test` on the same build, which never
registers a read-back, stops in the same place, and after a long session of hundreds of device
creations both stop earlier still, while querying device capabilities. That is a window or
device-setup stall in this environment rather than the backend: the headless tests keep passing,
and the same windowed tests passed earlier in this same session.

### 0e. The staged `spirv` directory can vanish while its stamp survives

`build/msvc/stage/Debug/spirv` was missing while the staging stamp was current, so a build
reported success and `physics_spirv_loader_test` failed with "Runtime data directory 'spirv' is
missing". Copying `build/msvc/engine/Physics/spirv/debug` over it restored the stage. The stamp
should be keyed on the directory existing, not on the bake alone.
### 0f. `complex_mesh_test` and `pbr_test` crashed in their first frame, and no longer do

Both died before `StartFrame`: their `PrepareGui` calls Dear ImGui directly, a WebGPU build has no
ImGui context (no UI backend - task 10.4), and the first `SetNextWindowPos` dereferenced it. They
were never read-back crashes; the read-back was simply the last thing the graph had built, and a
test's standard output is block buffered, so everything the test printed was lost with the process.

A guard at the top of each panel says what a build without a UI does - skip the panel, draw
nothing - and both tests now run to completion: `complex_mesh_test` 118270 of 921600 pixels not
black after 20 frames at 101 fps, `pbr_test` 716164 of 8294400 at 163 fps. `shadow_map_test`'s
control window is guarded the same way.

Fixed on the way and worth keeping: the widening copy released its staging buffer at the end of
the recording call, before submission, so the GPU was handed commands that read freed memory. It
is retired now and released when the command buffer that recorded it is destroyed, and a copy
whose image or buffer the backend cannot resolve is refused by name rather than passed on as a
null texture.
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

### 2. The browser renders the scene

The page draws the same frame the desktop does - the brick sphere over the ground, skybox behind
it - after sixty-odd frames, in the canvas. Two things were in the way:

- **A layout's non-reflective half was absent.** `SPLayout::Merge`, `GenerateLayoutBindings`,
  `GenerateAllLayoutBindings` and `PlaceBufferVariable` are arithmetic on the binding table, but
  they lived in the unit only a build with SPIRV-Cross compiles, and the browser replaced that
  unit with a stub that made all four no-ops. A material's layout was always empty there, its
  pipeline was built without the group its shaders use, and every draw of it was rejected. They
  are in `SPLayoutCore.cpp` now, which every build compiles; only `Reflect` is per-backend.
- **The diagnostic capture blocked the page.** Its map wait loop only pumped events, which never
  lets the browser deliver the map callback, so the second frame waited forever. It yields with
  `emscripten_sleep(0)` now, and the automatic probe the previous round added is gone - it was
  the thing being waited on.

Faults the browser surfaced on the way, all fixed and all real: libktx wrote eight bytes into a
four-byte `ktx_size_t` on a 32-bit target; the mesh archive wrote `size_t` and whole structs, so a
file written by a 64-bit build read as garbage in the browser; and `WriteVertexAttributeBuffer`
copied from an attribute offset added to an empty buffer's data pointer.

Experiments already spent, recorded so they are not repeated:

- `-sSAFE_HEAP=1` reports an unrelated alignment fault in the glTF loader, which reads unaligned
  on purpose where it may.
- `-sASSERTIONS=2` prints no stack for these aborts: the generated `abort()` prints the message
  only.
- **AddressSanitizer is what found the memory bugs.** It belongs in the existing
  `build/emscripten` configure -
  `cmake --preset emscripten -DCMAKE_CXX_FLAGS="-fsanitize=address -g" -DCMAKE_C_FLAGS="-fsanitize=address -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address -sALLOW_MEMORY_GROWTH=1"`
  - because a separate build directory cannot get past its reflection parser.
- `Module.ENV` does not reach `SDL_getenv` in the page. The skip switches are
  `MainClass::SetDiagnosticSkip` plus example arguments; the `NEEDRENAME_SKIP_*` variables work on
  the desktop only.
- The page's map callbacks need a yielding wait (`emscripten_sleep(0)`), and a wait loop that only
  pumps events hangs the frame it runs in.

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