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

The preset selects `ENGINE_RHI_BACKEND=webgpu` and builds the applications (physics and editor),
the Python bindings, the examples, the tests, and `webgpu_spike`. Debug executables go to
`build/webgpu/output/Debug/bin`.

The applications and the bindings are part of this build on purpose: the physics app is the only
target that drives the GPU solver end to end, and the joint path in particular is exercised by
`physics_app_actuator_stand_test`. A render-only build would not compile it at all.

## Run and verify

Start with the two small WebGPU-only targets:

```powershell
# Headless device, compute, readback, and offscreen rendering checks
build\webgpu\output\Debug\bin\webgpu_spike.exe

# SDL surface, compute, triangle, and presentation; exits after 30 frames
build\webgpu\output\Debug\bin\webgpu_clear_example.exe
```

Then exercise the complete engine:

```powershell
$env:NEEDRENAME_PRESENT_CAPTURE = "$PWD\build\webgpu\output\Debug\presented.bmp"
build\webgpu\output\Debug\bin\external_resource_loading_example.exe 30
Remove-Item Env:NEEDRENAME_PRESENT_CAPTURE -ErrorAction SilentlyContinue
```

A run resolves its data by proximity: `output/<config>/share/needrename` sits beside the binaries,
the same way an installed prefix keeps it beside `bin/`. On this backend that matters, because the
built-in shaders exist only as baked WGSL/SPIR-V there and a WebGPU build has no runtime GLSL
compiler. `NEEDRENAME_DATA_ROOT` still overrides the root for a run that wants a different one.

A successful run exits with code zero. `presented.bmp` records what the surface received, which is
stronger evidence than an offscreen render-target dump alone.

Then the full suite, which runs the assembled product's executables:

```powershell
ctest --test-dir build/webgpu -C Debug --output-on-failure
```

If the applications are built (they are, by default), the physics example supplies an additional
numeric simulation check:

```powershell
build\webgpu\output\Debug\bin\physics_rigid_body_example.exe 180
```

Every 60 frames it logs `probe z ... moved ...`; an increasing `moved` value demonstrates that the
solver is advancing.

Dawn's WebGPU library is linked statically, so there is no `webgpu_dawn.dll`. The build copies
`dxcompiler.dll` and `dxil.dll` from `DXC_ROOT/bin/x64` into `build/webgpu/output/Debug/bin` automatically,
and install rules include them in `bin`. `d3dcompiler_47.dll` loads directly from System32. No PATH
modification or manual per-executable copy is needed.
