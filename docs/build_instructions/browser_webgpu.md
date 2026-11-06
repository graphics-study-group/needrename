# Browser (WebGPU / Emscripten) Build Instructions

The browser build compiles the engine's WebGPU backend and its apps to wasm with Emscripten. It
produces a page per executable (`.html` + `.js` + `.wasm`, plus `.data` when assets are preloaded),
not a program.

## Dependencies

| Dependency | Provisioning | Purpose |
| --- | --- | --- |
| **emsdk** with the `emdawnwebgpu` port | https://emscripten.org (`emsdk install latest && emsdk activate latest`) | wasm toolchain; the WebGPU port supplies `webgpu/webgpu.h` and `emdawnwebgpu.js` |
| **Ninja** | on `PATH` or named by `CMAKE_MAKE_PROGRAM` | generator used by the `emscripten` preset |
| **Tint** | [manual installation](webgpu_dependencies.md) | native host SPIR-V to WGSL converter used by the shader bake |
| **Host `shader_baker`** | built by the `host-tools` preset (`build/host-tools/bin/Release/shader_baker.exe`) | a cross build cannot build or run the baker for its own target |
| Python + `.venv` | as for the Windows build | reflection parser |
| A Chromium browser with WebGPU | Edge or Chrome, hardware acceleration on | runs the page |
| Node.js 22+ | https://nodejs.org | native `fetch` and `WebSocket` used by the CDP capture helper |

Install Tint and set `DAWN_ROOT` once using [webgpu_dependencies.md](webgpu_dependencies.md). See
[environment_variables.md](environment_variables.md) for the complete preset-by-preset variable
matrix, including emsdk and Ninja. The browser build links no Dawn and no wgpu-native;
`emdawnwebgpu` supplies its WebGPU implementation, and the shader bake uses the native host
`tint.exe` from the Dawn prefix.

## Configure and build

Build the native host tools once - this replaces the old full `msvc` engine prerequisite:

```powershell
cmake --preset host-tools
cmake --build --preset host-tools
```

Then the browser build (the preset selects `ENGINE_RHI_BACKEND=webgpu` and builds the apps). With
`DAWN_ROOT` set, CMake discovers `bin/tint.exe` without an explicit converter argument:

```powershell
cmake --preset emscripten
cmake --build --preset emscripten
```

Artifacts land in `build/emscripten/bin`: `physics_rigid_body_example.html`,
`editor_run_game_example.html`, `external_resource_loading_example.html`, and
`webgpu_clear_example.html`. The first three preload assets and each has a `.data` file alongside
its `.js` and `.wasm` files.

The staged data root (`build/emscripten/stage/<config>`) is what `--preload-file` reads for
`builtin_assets` and the baked `spirv` tree; every page that preloads it depends on the staging
target, so a clean parallel build needs no manual staging command. Shader outputs are per-source
custom commands: adding a GLSL file under `engine/Physics/shader/` is picked up by the next
configure, and editing one source or shared include rebuilds exactly its outputs.

## Run

A page loads its `.data` and `.wasm` over HTTP, so serve the output directory rather than opening
the file:

```powershell
.venv\Scripts\python.exe -m http.server 8098 --bind 127.0.0.1 --directory build/emscripten/bin
# open http://127.0.0.1:8098/physics_rigid_body_example.html
```

`physics_rigid_body_example` and `editor_run_game_example` read the URL:

| Switch | Meaning |
| --- | --- |
| `?frames=N` | run N frames and stop (the physics example runs until the page closes otherwise) |
| `?warmup=N` | (physics example) step the solver N times before the first presented frame |
| `?cam=N` | (physics example) shift the camera by N units; a capture that reflects it is a fresh capture |

## Capture and verify

A page with a frame loop never "finishes", so `--screenshot` decides badly when to capture. Drive
the DevTools protocol instead; `tools/dsh/cdp_shot.mjs` waits, captures the canvas, and prints the
page's console:

```powershell
# point $edge at your Edge or Chrome executable
$edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$p = Start-Process $edge -ArgumentList @(
    '--headless=new', '--enable-unsafe-webgpu', '--no-sandbox', '--no-first-run',
    '--remote-debugging-port=9223', "--user-data-dir=$PWD\build\emscripten\profile", 'about:blank'
) -PassThru
node tools/dsh/cdp_shot.mjs http://127.0.0.1:9223 "http://127.0.0.1:8098/physics_rigid_body_example.html" shot.png 20000 1280 1000
taskkill /T /F /PID $p.Id
```

The physics example logs a numeric probe every 60 frames (`probe z ... moved ...`); that line is the
evidence that the simulation is advancing, independently of what the picture looks like.

## Target limits this build assumes

Measured on Windows/Edge (D3D12): `maxStorageBuffersPerShaderStage = 16`,
`maxStorageTexturesPerShaderStage = 8`, `maxBindGroups = 4`,
`maxComputeInvocationsPerWorkgroup = 1024`. The physics kernels are shaped to stay inside 16 storage
buffers per stage, and **WebGPU rejects one buffer bound to two storage bindings of a dispatch when
either is writable**; the WebGPU backend rejects that on the desktop validation build too
(`ComputeKernel::DispatchAndRecord`).

## Known limitations

- No automated browser test: `ctest` does not run wasm. Verification is a CDP capture plus the
  page's console.
- The editor's project is a memory filesystem (`/temp_project`): edits and imports do not survive a
  refresh until the project tree is mounted on IDBFS.
- `emscripten_sleep` needs Asyncify, which every browser example links; a page whose loop never
  yields never receives the WebGPU callbacks it polls for.
