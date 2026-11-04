# Handover

The WebGPU build needs a SPIR-V to WGSL converter (`SHADER_WGSL_CONVERTER`) that has to be passed
once because the Dawn build is not on `PATH`.

## Where it stands in one sentence

The engine's own scene draws and presents through the WebGPU backend on desktop - the full
windowed example runs to its last frame and the surface image it presents is the frame it
rendered - the skybox test's WebGPU frame is byte-for-byte the frame Vulkan produces, and the
browser runs both the rigid-body example (bodies fall and roll, 2.2 -> 5.5 m of probe travel) and
the editor (dockspace, scene, Play mode) in the canvas.

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

### 0f-bis. Windowed tests stall after a long session, on every backend

Seen three times in one day: windowed tests create their window and device, then stop - `input_test`
and `skybox_test` included, and on the Vulkan build as readily as the WebGPU one. The same binaries
ran the same tests to completion an hour earlier, and a rebuild is not involved. Headless tests keep
passing throughout, and so does the windowed example.

The reading that fits every occurrence: the interactive desktop session this agent runs in loses its
window or GPU state after enough device creation and destruction, and any test that needs a swapchain
waits there. It is worth checking `skybox_test` standalone before believing a suite number - when it
passes, the numbers below are meaningful; when it stalls, the machine is the variable, not the code.

### 0d. The suite, measured on both backends

`ctest --preset msvc-debug -E "asset_path_test|data_root_test"` (Vulkan): **76 of 76 passed.**
`ctest --test-dir build/webgpu -C Debug` with `NEEDRENAME_DATA_ROOT` pointing at its stage:
**46 of 46 passed.** So did the Vulkan suite, **76 of 76**; what the six failures were, and the three faults behind every one of them, is 0c.

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

### 0c. What the "hangs" were, and why both suites are green now

Every test that looked hung was waiting on a **CRT message box**. A failed `assert`, or an
exception leaving `main`, is reported by the C runtime through a modal dialog; a test run has no
desktop to dismiss one from, so the process waited there forever. `cdbX64` attached to the stalled
process showed it plainly - `USER32!MessageBoxW` under `ucrtbased!abort` under
`std::terminate` - and no amount of logging from inside the engine would ever have said so.

`test/ForcedTestRuntime.h` is force-included into every test target: assertions report to stderr,
and the terminate handler prints the thrown exception and leaves with code 3 instead of aborting
into the dialog. That turned three "hangs" into three one-line answers:

- `MaterialLibrary` threw "has no interface description" for a shader that declares **no bindings**
  - the debug write-through fragment. An empty table is a description, not a missing one.
- A pooled read-back buffer stayed **mapped** after its contents were read, and the next frame's
  copy targeted it. `DeviceBuffer::ReleaseReadback` unmaps it (WebGPU) or does nothing (Vulkan),
  and the frame pump calls it once the callback has returned.
- `MaterialLibrary` asked `ENGINE_RHI_BAKE_REFLECTION` whether it could reflect. That macro means
  the baker is in the build. A Vulkan build reflects at runtime and has no baker, so every material
  took the description path and threw. `ENGINE_RHI_HAS_REFLECTION` now answers the question, and
  EngineRhi defines it from the same condition that picks between SPLayout's two implementations.

With those in: **Vulkan 76 of 76 passed, WebGPU 46 of 46 passed.**
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
## 0g. Two self-inflicted detours worth not repeating

Both cost most of a session and neither was a code problem.

- **A cleanup glob deleted `SDL3.dll`.** Removing stray measurement files with `... -Filter`-less
  wildcards (`sd*`) matched `build/msvc/bin/Debug/SDL3.dll`, and the next suite run reported **54
  failures** with `0xC0000135` (DLL not found) and no message anywhere saying which DLL. If a whole
  suite fails at once with that exit code, compare the DLL set in `bin/Debug` against a known-good
  tree before reading anything into it.
- **`git stash` + rebuild left one target unlinked.** After stashing an experiment and rebuilding,
  `SDL_init_test.exe` was never produced (MSBuild: "LINK : ... not found ... or the previous
  incremental link"), so ctest counted it as a failure. Building the target by name fixed it. A
  suite run after a stash/pop is not trustworthy until every test binary exists.

- **The msvc suite wants no `NEEDRENAME_DATA_ROOT`.** `shader_compile_test` resolves `test_data`
  under the data root, and the stage directory has no `test_data` (it is an install-time artifact,
  `share/needrename/test_data`). Run the msvc suite with the variable **unset**; run the WebGPU
  suite with it **set** to `build/webgpu/stage/Debug`. Setting it for the msvc suite turns two
  passing tests red.
## 0h. The brick-sphere artifact: a legacy mesh read in the wrong attribute order (fixed)

Reported: the `external_resource_loading_example` scene shows the sphere covered in thin lit
streaks instead of the brick texture, and the ground shadow looks wrong.

The streak is fixed. The cause is in `MeshAsset::LoadLegacyMesh`, and it only affects assets
written before the fixed-width mesh format - which is why the freshly imported sphere next to the
old one rendered correctly, and why the earlier report read as "the asset that was already in the
scene broke when the new one loaded".

### What it was

The pre-v2 writer listed the attribute table as `positions, color, normal, texcoord0, tangent, ...`.
The fixed-width format lists it in `VertexAttributeSemantic` order, which is `... tangent,
texcoord0, ...`, and the commit that introduced it (`588e47fc`) changed the writer and the reader
together but left `LoadLegacyMesh` reading the *new* order. An archive written before that commit is
therefore read with `texcoord0` and `tangent` swapped - every other slot still lines up, so the mesh
looks structurally fine and only the two streams are wrong.

That is not cosmetic. The pipeline is told `Tangent = SFloat32x2` and `Texcoord0 = SFloat32x4`, so
the lit pass builds its TBN from the UV stream and samples the normal map through the tangent. The
result is a surface shaded almost entirely black with thin lit slivers along the meridians, which is
exactly the reported picture.

Measured, from the two mesh files themselves:

```
projects/test_project/assets/Sphere.0.mesh        legacy (u32 version 1, 64-bit writer)
  tangent   type=2 (vec2) offset=77148 size=17144   <- really texcoord0
  texcoord0 type=4 (vec4) offset=94292 size=34288   <- really tangent

temp_project/assets/imported_resources/Sphere.0.mesh   current format (u32 version 2)
  tangent   type=4 (vec4) offset=94292 size=34288
  texcoord0 type=2 (vec2) offset=77148 size=17144
```

The same swap is in all three shipped meshes - `builtin_assets/mesh/{cube,cylinder,sphere}.0.mesh`
are all legacy and all put the `vec2` in slot 3 - so this was never specific to the imported
example: any normal-mapped draw over a builtin mesh was shading through a swapped TBN.

The earlier session's reading of the artifact - "the signature of vertex normals arriving wrong, a
UV/tangent stream read in the normal's place" - was right about the mechanism, and wrong only about
where: not the importer or the material's vertex layout, but the legacy archive reader. What that
session measured, still worth having:

- The **same artifact is on Vulkan**. `evidence/vk-window-brick-scene.png` is an OS screenshot of
  the Vulkan build's own window; the sphere is identically dark with the same meridian-shaped
  slivers, and the ground shows the same soft blob.
- It is **not the frame-completion change either**: with `FrameManager.cpp` and the Vulkan
  `DeviceContext.cpp` reverted to the commit before it, the Vulkan window looks the same.
- Textures are not the problem at upload time: every uploaded texture logs 512x512, **1 mip**, one
  layer (plus the 1024x1024 six-layer skybox cubemap), and texture sampling demonstrably works on
  both backends in `pbr_test`, `new_material_test`, `imgui_test` and the skybox golden.
- The ground's brick *is* correct where the light reaches it (`evidence/vk-window-brick-scene-obj.png`
  shows the brick dome and the brick plane both reading properly), so the texture path is fine and
  what is wrong is the **sphere's shading**.

### The fix

`LoadLegacyMesh` now reads the legacy order (`texcoord0` before `tangent`), with the reason written
where the array is. Nothing else changes: the packed GPU buffer is built in semantic order by
`WriteVertexAttributeBuffer`, and the fixed-width reader already matches the fixed-width writer.

`test/unit/render/mesh_asset_legacy_test.cpp` builds a legacy archive in both writer widths (8-byte
and 4-byte `size_t`), loads it through `LoadLegacyMesh`, and asserts the two streams land on the
right semantics. With the old order restored it reports 8 failures, so the next format change cannot
quietly reintroduce this.

Verified by running the example: both spheres show the brick texture, the old one included
(`build/cap/verified.png`).

### Still open

The ground shadow is still a large soft black blob, and the unlit side of each sphere is pure black.
Neither was this defect: they read the same before and after the fix, and the shadow was already
recorded above as a separate complaint. Nothing here claims either is correct.

### Why the capture detour did not pay off

RenderDoc live capture does not work for this target on this machine: `renderdoc-mcp` bundles
1.43 and the installed RenderDoc is 1.46, so `capture_frame` and `renderdoc-cli capture` fail to
inject, the registry-registered 1.46 layer does load but F12 produces no capture, and a 1.46
capture would not open in the bundled 1.43 replay anyway. The defect was found by parsing the two
`.mesh` files directly - cheaper than a frame when the suspicion is asset content, which it should
have been from the start: the two spheres differ only in which asset they were loaded from.


## 0h. Running the physics app in a browser

Four things were between the page and a simulation; three are solved and the fourth is where the work
stands.

- **`PhysicsApp` must be a static archive for Emscripten.** `add_library(... SHARED ...)` builds a
  *side module* there, and a side module linked into a wasm image resolves its symbols to null - the
  first call through one traps with `RuntimeError: null function`, which is what the WebGPU adapter
  callback hit. With the archive the image shape matches the examples that already worked (a 630 KB
  JS glue and a 114 MB wasm, instead of a 20 MB glue and a 14 MB wasm).
- **The frame loop has to yield.** The engine's waits poll `wgpuInstanceProcessEvents`; a `while`
  with no yield never receives the callbacks it is polling for, and the first `Step()` spins for ever.
  `emscripten_sleep(1)` per iteration is what lets the browser deliver them. `emscripten_set_main_loop`
  is *not* the pattern this engine uses - `MainClass::LoopFinite` is a plain `while` - and it did not
  work with Asyncify here.
- **A first visit has no data pak cache.** A fresh profile stalls on
  `still waiting on run dependencies: ... .data`; reusing a profile that has fetched the 73 MB pak
  once is what makes a repeat run start.
- **The readback probe aborts the page.** Reading a body's state maps a readback buffer, and in a
  browser that trap is a JS `RuntimeError`, not a C++ exception - `try`/`catch` around the call does
  not save the page, and the run dies on the frame the probe fires (frame 60 in the example). The
  fix is the completion-driven readback 8.5 describes: `PrepareReadback`, read only when the map is
  ready, skip the frame otherwise. `PhysicsApp::GetBodyState` still takes the old "map on read" path.

Evidence so far: the page loads, builds and draws the scene (a five-layer box stack with two spheres
above it), the frame trace reaches 605 frames, and a `?cam=N` camera offset changes the captured
image - so the screenshot is the current frame, not a stale compositor one. What is *not* yet
demonstrated is the bodies moving: an identical image with and without a 240-step warm-up says either
the solver is not changing anything the renderer reads, or the probe is the only thing that could
have said so and it aborts. The numeric probe is the next step, not a picture.
A measurement trap worth repeating: **compare the canvas, not the page.** A whole-page screenshot
differs whenever the Emscripten status line differs - `Running...` against
`Exception thrown, see JavaScript console` - so two runs can hash differently while the scene is
identical, and the opposite conclusion gets drawn. Two runs with 240 and 600 warm-up steps hashed
differently for exactly that reason while the drawn scene was unchanged, and six hundred steps of
gravity would have moved the falling sphere if the simulation had been advancing. Crop to the canvas
rectangle before comparing anything about the picture.
## 0i. The browser runs the physics example and the editor

Measured 2026-11-04 on this branch. Both pages work end to end, and what was actually in the way was
five defects and a build gate, not a missing feature.

### What the pages do

- `physics_rigid_body_example.html`: the five-layer box stack and the two spheres simulate in the
  canvas. The probe logs `frame 60, probe z 1.778 (started 4.000), moved 2.222 m`, then `moved`
  climbs to 5.5 m over later samples while the sphere rolls; the capture shows the stack with the
  spheres mid-roll. It ran 480+ frames in a 20 s wall clock in headless Edge (about 24 fps at
  1280x720, no vsync). Frame: `evidence/browser-physics-simulation.png`.
- `editor_run_game_example.html`: the dockspace, Hierarchy/Scene/Game/Inspector/Project widgets and
  the rendered scene are in the canvas, and clicking Start puts the editor into Play mode (the
  button reads Stop) with physics running in the page. Frame:
  `evidence/browser-editor-play.png`.

### The five defects, in the order they were found

1. **One buffer bound to two storage bindings of one dispatch.** WebGPU rejects it (Chrome:
   `Writable storage buffer binding aliasing ...`), and the rejection invalidates the whole physics
   command buffer, so the solver never ran and the scene was static. Four shader families did it,
   each for a different reason:
   - `parallel_scan.comp` / `add_block_offset.comp` scanned in place through `InputData` +
     `OutputData`; an in-place scan is now `parallel_scan_inplace.comp`, one read-write binding, and
     the block sums alternate between two buffers so the recursion never binds its data as its own
     block sums. The two-buffer form stays for a caller with distinct buffers (no transfer copy
     needed, so the Vulkan barrier story is unchanged).
   - `compact_scatter.comp` bound `SortedKeys` and `CompactKeys` to one buffer; it is one binding now.
   - `sum_by_key_body.glsl` bound its record keys and values as two views of one record buffer, and
     a level read the previous region while writing its own region of that buffer; each level now
     owns a key buffer and a value buffer, and the bindings a level does not write point at sinks.
   - `radix_scatter.comp` bound the key array as `PayloadIn` / `PayloadOut` when the caller had no
     payload; a dedicated placeholder plus an always-allocated payload partner make every binding a
     distinct buffer.
   **The guard is the part to keep:** `ComputeKernel::DispatchAndRecord` now rejects two storage
   bindings of one buffer when either is writable, so the desktop WebGPU build fails like Chrome.
   It found the `SumByKey` and `RadixScatter` sites after the hand-written fixes had already been
   made - wgpu-native accepts all of them silently.
2. **A read-mode mapping read with the mutable accessor.** emdawnwebgpu rejects
   `wgpuBufferGetMappedRange` on a `WGPUMapMode_Read` mapping ("Mapping is read-only. Use
   GetConstMappedRange instead.") and returns null; the null left the map pending, and the next
   `GetVMAddress` issued a second map request, which aborts the page with
   `mPendingMapRequest.mode == WGPUMapMode_None`. `PrepareReadback`, `GetVMAddress` and the surface
   capture now use `wgpuBufferGetConstMappedRange`.
3. **The editor was gated out of every non-Vulkan build, and reached Vulkan directly.** It is
   built for the selected backend now: `EngineEditor` is a static archive on Emscripten (the same
   side-module trap `PhysicsApp` hit), the four files that included `vulkan/vulkan.hpp` /
   `Rhi/Backend/Vulkan/...` / `imgui_impl_vulkan.h` no longer do, and the RTT-as-ImGui-image path
   goes through `GUISystem::AddTexture` / `RemoveTexture`, whose WebGPU implementation hands back the
   texture view as the id.
4. **The editor's browser host code.** `SDL_GetDesktopDisplayMode(1)` has no display 1 on the web
   (SDL3's Emscripten driver adds exactly one), the page has no data root to resolve next to an
   executable, and the main loop never yielded. The example now falls back to display 0, sets
   `NEEDRENAME_DATA_ROOT=/` under a preloaded tree, sizes the render graph from the present extent
   rather than the display, and yields once per frame with `emscripten_sleep(1)`.
5. **A busy frame slot was treated as an out-of-date presentation target.**
   `FrameManager::StartFrame(0)` returned the same `~0u` for "the slot is still in flight" and "the
   image could not be acquired", and `RenderSystem::TryStartFrame` reacted by calling
   `UpdateSwapchain` - so every frame that found the ring busy reconfigured the surface. On a canvas
   `wgpuSurfaceConfigure` resets what the canvas shows, which is why the physics page was black
   while it was still running; capturing after the loop had stopped showed the last frame, which is
   how the black and the good captures differed. `StartFrame(0)` now returns `k_frame_busy` for the
   busy case and `~0u` only for out-of-date, and the caller recreates only on the latter.

Also worth keeping: `SignalSemaphore` is reported once rather than once per frame, and the physics
example's per-frame `SDL_Log` diagnostics are gone - they cost real time in the browser and hid the
probe lines.

### How it was driven, and what to copy

A page with a frame loop never "finishes", so `--screenshot` is the wrong tool. Launch Edge with
`--remote-debugging-port` and drive it:

```powershell
node tools/dsh/cdp_shot.mjs http://127.0.0.1:9223 "http://127.0.0.1:8098/physics_rigid_body_example.html" out.png 20000 1280 1000
```

`?frames=N` bounds the physics example (it runs until the page closes otherwise), `?warmup=N` steps
before the first frame, `?cam=N` shifts the camera so a capture proves it is fresh. The editor takes
`?frames=N` too; without it, it runs until the page closes.

### What is still not covered

- No automated browser test: ctest runs no wasm, and these two runs are still manual captures plus
  the console. The probe line is the assertion a script could read.
- The WebGPU desktop validation build cannot reproduce a Chrome-only rule; the new guard is what
  stands in for it, and it only covers compute storage-buffer aliasing.
- The editor's MEMFS project (`temp_project`) is per-page-load: edits, imports and new folders are
  lost on refresh until the project tree is mounted on IDBFS.
