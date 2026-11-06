# Desktop WebGPU Build Instructions

The desktop WebGPU build uses the MSVC toolchain and Dawn. It is currently a validation and A/B
environment; the Vulkan build remains the behavioral reference.

## Dependencies

Complete the native Windows setup in [windows_msvc.md](windows_msvc.md), including Visual Studio,
the Clang tools component, SDL3 and Python. The Vulkan SDK is **not** needed: physics shaders are
compiled by the build's own native `shader_baker`, and the WebGPU implementation is Dawn's static
library.

Install DXC and set `DXC_ROOT` as described in [webgpu_dependencies.md](webgpu_dependencies.md).
With no Dawn path configured, CMake automatically downloads pinned Debug and Release packages on
first configure. `DAWN_ROOT` or `Dawn_DIR` can instead select a manual package. The browser-only
emsdk dependency is not needed for this build. All variables used by the Windows and WebGPU presets
are summarized in [environment_variables.md](environment_variables.md).

## Configure and build

The build is self-contained: it produces and uses its own `shader_baker`, and reads nothing from
another preset's build tree.

```powershell
cmake --preset webgpu-desktop
cmake --build --preset webgpu-desktop
```

The preset selects `ENGINE_RHI_BACKEND=webgpu`, builds examples, tests, and `webgpu_spike`, and
writes Debug executables to `build/webgpu/bin/Debug`.

The preset disables the physics and editor applications. To include their examples as well:

```powershell
cmake --preset webgpu-desktop -DENGINE_BUILD_APPS=ON
cmake --build --preset webgpu-desktop
```

## Run and verify

Start with the two small WebGPU-only targets:

```powershell
# Headless device, compute, readback, and offscreen rendering checks
build\webgpu\bin\Debug\webgpu_spike.exe

# SDL surface, compute, triangle, and presentation; exits after 30 frames
build\webgpu\bin\Debug\webgpu_clear_example.exe
```

Then exercise the complete engine against the staged data root:

```powershell
$env:NEEDRENAME_DATA_ROOT = (Resolve-Path build\webgpu\stage\Debug).Path
$env:NEEDRENAME_PRESENT_CAPTURE = "$PWD\build\webgpu\presented.bmp"
build\webgpu\bin\Debug\external_resource_loading_example.exe 30
Remove-Item Env:NEEDRENAME_PRESENT_CAPTURE -ErrorAction SilentlyContinue
```

A successful run exits with code zero. `presented.bmp` records what the surface received, which is
stronger evidence than an offscreen render-target dump alone.

Run the focused staged-asset and cross-backend image checks, then the full suite:

```powershell
ctest --test-dir build/webgpu -C Debug `
    -R 'skybox_golden_test|mrt_test_staged_builtin_assets' `
    --output-on-failure
ctest --test-dir build/webgpu -C Debug --output-on-failure
```

Keep `NEEDRENAME_DATA_ROOT` pointed at `build/webgpu/stage/Debug` for the WebGPU suite. Do not use
that override for the Vulkan `msvc` suite.

If `ENGINE_BUILD_APPS=ON`, the physics example supplies an additional numeric simulation check:

```powershell
build\webgpu\bin\Debug\physics_rigid_body_example.exe 180
```

Every 60 frames it logs `probe z ... moved ...`; an increasing `moved` value demonstrates that the
solver is advancing.

Dawn's WebGPU library is linked statically, so there is no `webgpu_dawn.dll`. The build copies
`dxcompiler.dll` and `dxil.dll` from `DXC_ROOT/bin/x64` into `build/webgpu/bin/Debug` automatically,
and install rules include them in `bin`. `d3dcompiler_47.dll` loads directly from System32. No PATH
modification or manual per-executable copy is needed.
