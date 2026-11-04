# Browser (WebGPU / Emscripten) Build Instructions

The browser build compiles the engine's WebGPU backend and its apps to wasm with Emscripten. It
produces a page per executable (`.html` + `.js` + `.wasm` + `.data`), not a program.

## Dependencies

| Dependency | Provisioning | Purpose |
| --- | --- | --- |
| **emsdk** with the `emdawnwebgpu` port | https://emscripten.org (`emsdk install latest && emsdk activate latest`) | wasm toolchain; the WebGPU port supplies `webgpu/webgpu.h` and `emdawnwebgpu.js` |
| **Tint** | a Dawn release archive (e.g. `Dawn-<hash>-windows-latest-Release/bin/tint.exe`) | SPIR-V to WGSL converter used by the shader bake |
| **Host `shader_baker`** | built by a native Vulkan configure (`build/msvc/bin/Debug/shader_baker.exe`) | a cross build cannot build or run the baker for its own target |
| **glslangValidator** | Vulkan SDK | compiles the physics GLSL to SPIR-V during the build |
| Python + `.venv` | as for the Windows build | reflection parser |
| A Chromium browser with WebGPU | Edge or Chrome, hardware acceleration on | runs the page |

The machine paths this repository's tree was set up with are listed in
`openspec/changes/rhi-webgpu-backend/HANDOVER.md` ("Machine setup this expects").

## Configure and build

A Vulkan configure must have built the host baker once:

```powershell
cmake --preset msvc
cmake --build --preset msvc-debug --target shader_baker
```

Then the browser build (the preset selects `ENGINE_RHI_BACKEND=webgpu` and builds the apps):

```powershell
cmake --preset emscripten -DSHADER_WGSL_CONVERTER=D:/tools/dawn/<dawn>/bin/tint.exe
cmake --build build/emscripten
```

Artifacts land in `build/emscripten/bin`: `physics_rigid_body_example.html`,
`editor_run_game_example.html` and `external_resource_loading_example.html`, each with its `.js`,
`.wasm` and `.data`.

The staged data root (`build/emscripten/stage/<config>`) must exist before a page is linked: it is
what `--preload-file` reads for `builtin_assets` and the baked `spirv` tree.

> The bake is a stamped custom command. After adding a shader, delete
> `build/emscripten/engine/Physics/spirv/debug/.wgsl_stamp` so the new module is baked; the stamp
> does not depend on individual shader files.

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
