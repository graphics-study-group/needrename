# WebGPU Native Dependencies (Dawn, Tint, and DXC)

Desktop Windows WebGPU can provision Dawn automatically. DXC remains a small manual dependency;
browser builds need a manual Dawn prefix only for the native Tint executable. CMake never searches
`PATH` or arbitrary SDK directories. For the complete environment matrix, see
[environment_variables.md](environment_variables.md).

## Known-good versions

| Dependency | Version | Purpose |
| --- | --- | --- |
| [Dawn / Tint](https://github.com/google/dawn/releases/tag/v20261105.214757) | `v20261105.214757`, revision `dfc836b2dea35488fd0488302b27cd47f0ffcec1` | Desktop WebGPU implementation and build-time SPIR-V to WGSL conversion |
| [DirectX Shader Compiler](https://github.com/microsoft/DirectXShaderCompiler/releases) | Current signed Windows release | Runtime HLSL-to-DXIL compilation for Dawn's D3D12 backend |

Dawn upgrades are separate, testable changes: the tag, revisioned archive names, and SHA-256 hashes
are pinned in `cmake/DawnPrebuilt.cmake`. The official nightly archives are best-effort binaries, so
CMake verifies every downloaded archive before extraction.

## Automatic desktop Dawn setup (Windows MSVC)

Leave `DAWN_ROOT` and `Dawn_DIR` unset. The desktop configure downloads the matching Debug and
Release archives into:

```text
build\_deps\dawn\v20261105.214757\
    debug\
    release\
```

The first download is approximately 444 MB. Completed prefixes are reused by later configures and
build trees; incomplete downloads are never accepted. Debug targets link the Debug library, Release
targets link the Release library, and RelWithDebInfo/MinSizeRel map to Release. Tint comes from the
Release prefix. No manual installation or environment variable is needed for Dawn in this flow.
The official Debug archive contains a Debug library but no PDB files, so MSVC can emit `LNK4099`
for Dawn's own missing symbols; engine targets still use the normal Debug runtime and debug info.

Automatic provisioning currently covers only native Windows MSVC. Other platforms must provide a
matching package through `DAWN_ROOT` or `Dawn_DIR`. Emscripten never downloads or links desktop Dawn.

## Optional manual Dawn layouts

A dual-config `DAWN_ROOT` contains matching packages from the same revision:

```text
C:\build_tools\dawn\
    debug\
        include\webgpu\webgpu.h
        lib\webgpu_dawn.lib
        lib\cmake\Dawn\DawnConfig.cmake
    release\
        include\webgpu\webgpu.h
        lib\webgpu_dawn.lib
        lib\cmake\Dawn\DawnConfig.cmake
        bin\tint.exe
```

The original single-package Release layout remains supported:

```text
C:\build_tools\dawn\
    include\webgpu\webgpu.h
    lib\webgpu_dawn.lib
    lib\cmake\Dawn\DawnConfig.cmake
    bin\tint.exe
```

With a Release-only package, Debug engine targets use the Release MSVC runtime for compatibility.
A dual-config package uses the normal Debug and Release runtimes instead. An explicitly invalid
`DAWN_ROOT` is an error; CMake does not silently replace it with another Dawn version.

For a nonstandard package location, set `Dawn_DIR` to the directory containing `DawnConfig.cmake`.
It takes precedence over `DAWN_ROOT`. `SHADER_WGSL_CONVERTER` can independently name Tint.

## Tint for browser builds

The browser target uses Emscripten's `emdawnwebgpu` port, but shader baking still needs native host
Tint. Set `DAWN_ROOT` to either manual layout above; the `release` prefix is selected automatically
in a dual layout. Alternatively set `SHADER_WGSL_CONVERTER` directly to `tint.exe`.

```powershell
setx DAWN_ROOT "C:\build_tools\dawn"
```

Open a new terminal after using `setx`. Desktop automatic provisioning does not supply Tint to the
browser configure because that configure intentionally has no desktop-Dawn dependency.

## Install DXC for desktop WebGPU

Download a prebuilt archive from
[DirectXShaderCompiler Releases](https://github.com/microsoft/DirectXShaderCompiler/releases) and
extract it without changing its layout:

```text
C:\build_tools\dxc\
    bin\x64\dxcompiler.dll
    bin\x64\dxil.dll
    inc\
    lib\
```

Set `DXC_ROOT` to the extraction root, not to `bin` or `bin\x64`:

```powershell
setx DXC_ROOT "C:\build_tools\dxc"
```

CMake reads exactly `DXC_ROOT/bin/x64/dxcompiler.dll` and `DXC_ROOT/bin/x64/dxil.dll`, copies both
beside desktop WebGPU executables with `copy_if_different`, and includes them in the installed `bin`
directory. Do not copy them into the Dawn directory. `d3dcompiler_47.dll` remains in System32.

## Verify

For automatic desktop provisioning, a successful configure prints the package directory beneath
`build/_deps/dawn`, `Dawn Debug library: ON`, the Release Tint path, and the DXC runtime path.

For a manual single package, check:

```powershell
Test-Path "$env:DAWN_ROOT\lib\cmake\Dawn\DawnConfig.cmake"
Test-Path "$env:DAWN_ROOT\lib\webgpu_dawn.lib"
Test-Path "$env:DAWN_ROOT\bin\tint.exe"
Test-Path "$env:DXC_ROOT\bin\x64\dxcompiler.dll"
Test-Path "$env:DXC_ROOT\bin\x64\dxil.dll"
```

There is no `webgpu_dawn.dll`. The two DXC DLLs are application-local runtime components and must
remain beside the installed executables; FXC remains a system component.
