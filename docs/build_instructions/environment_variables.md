# Windows and WebGPU Environment Variables

This page collects the environment variables used by the Windows, desktop WebGPU, and browser
builds. Set persistent user variables with Windows' environment-variables dialog or `setx`, then
open a new terminal. Do not add DLL directories to `PATH`: the build copies application-local DLLs
or loads Windows system components directly.

The example installation root is `C:\build_tools`; other locations are supported. Paths below are
examples, not values hard-coded in the project.

## Variables by build target

| Build preset | Required environment | Optional environment | Notes |
| --- | --- | --- | --- |
| `msvc` (Vulkan) | `VULKAN_SDK`, `SDL3_ROOT` | — | The Vulkan SDK installer normally sets `VULKAN_SDK` |
| `host-tools` | `SDL3_ROOT` | — | Native shader tools use SDL logging |
| `webgpu-desktop` | `SDL3_ROOT`, `DXC_ROOT` | `DAWN_ROOT` | Without a Dawn path, Windows MSVC downloads pinned Debug/Release packages |
| `emscripten` | `DAWN_ROOT`, `EMSDK` or `EMSDK_ROOT`; Ninja on `PATH` | — | Build `host-tools` first; desktop Dawn and DXC are not used |

## Required variables

### `VULKAN_SDK`

The LunarG Vulkan SDK root, for example `C:\VulkanSDK\1.4.313.2`. Required only by the Vulkan
engine build. The SDK provides headers, the loader, validation layers, and development tools.
Desktop and browser WebGPU do not use this variable.

### `SDL3_ROOT`

The extracted SDL3 VC development package root. The exact variable name is `SDL3_ROOT`, not
`SDL_ROOT`. It must contain `cmake\SDL3Config.cmake`; the package also supplies the SDL3 DLL and
import library.

Native Windows builds and `host-tools` require it. Browser target code uses Emscripten's SDL3 port,
but the native host-tools prerequisite still uses this variable.

### `DXC_ROOT`

The root of an extracted prebuilt
[DirectXShaderCompiler release](https://github.com/microsoft/DirectXShaderCompiler/releases).
Required only by desktop WebGPU on Windows. It must contain:

```text
bin\x64\dxcompiler.dll
bin\x64\dxil.dll
```

Set the variable to the extraction root, not to `bin` or `bin\x64`. CMake reads only these two
fixed paths, copies both DLLs beside executables, and includes them in installations.
`d3dcompiler_47.dll` remains in System32 and is not copied.

### `EMSDK` or `EMSDK_ROOT`

The activated emsdk root, required only for the browser build. `EMSDK` is emsdk's standard variable;
the toolchain also accepts `EMSDK_ROOT`. The root must contain
`upstream\emscripten\cmake\Modules\Platform\Emscripten.cmake`.

Ninja must additionally be available on `PATH`, unless `CMAKE_MAKE_PROGRAM` is set explicitly as a
CMake cache variable.

## Dawn path: optional on desktop, required for browser Tint

### `DAWN_ROOT`

For desktop Windows MSVC, leave this unset to download pinned Dawn Debug and Release packages into
`build/_deps/dawn`. Set it only to select a manual package. Both the original single Release layout
and a dual layout with matching `debug/` and `release/` prefixes are supported. An invalid explicit
path is an error rather than a request to download another version.

Browser WebGPU links Emscripten's `emdawnwebgpu` port instead of desktop Dawn, but still requires
native Tint for shader baking. Set `DAWN_ROOT` to either manual layout; a dual layout uses its
`release/bin/tint.exe`. Alternatively use the `SHADER_WGSL_CONVERTER` CMake cache override. The
browser configure never triggers the desktop download.

`Dawn_DIR` is the CMake cache override for a directory containing `DawnConfig.cmake` and takes
precedence over `DAWN_ROOT` for native builds.

See [webgpu_dependencies.md](webgpu_dependencies.md) for exact layouts, archive sizes, hashes, and
verification commands.

## Other optional variables

| Variable | Purpose |
| --- | --- |
| `VK_LAYER_PATH` | Vulkan validation-layer discovery for Debug runs; normally the SDK installation is sufficient, and missing layers are skipped gracefully |
| `NEEDRENAME_DATA_ROOT` | Runtime data-root override used by selected tests and development runs; not a build dependency |
| `LIBCLANG_LIBRARY_PATH` / `LIBCLANG_LIBRARY_FILE` | Optional reflection-parser overrides; normally CMake uses the Clang component installed with Visual Studio |

Python is configured through the preset's `Python3_EXECUTABLE` CMake cache entry, not an environment
variable. See [windows_msvc.md](windows_msvc.md) for the `.venv` setup and its local-preset override.

## Example setup

```powershell
setx VULKAN_SDK "C:\VulkanSDK\1.4.313.2"  # normally already set by its installer
setx SDL3_ROOT "C:\build_tools\SDL3"
setx DXC_ROOT "C:\build_tools\dxc"          # desktop WebGPU only
setx EMSDK "C:\build_tools\emsdk"           # browser builds only
setx DAWN_ROOT "C:\build_tools\dawn"        # optional on desktop; browser Tint
```

Open a new terminal after using `setx`. Verify only the layouts relevant to the selected build:

```powershell
Test-Path "$env:VULKAN_SDK\Include\vulkan\vulkan.hpp"
Test-Path "$env:SDL3_ROOT\cmake\SDL3Config.cmake"
Test-Path "$env:DXC_ROOT\bin\x64\dxcompiler.dll"
Test-Path "$env:DXC_ROOT\bin\x64\dxil.dll"
Test-Path "$env:EMSDK\upstream\emscripten\cmake\Modules\Platform\Emscripten.cmake"
```

Detailed instructions are in [windows_msvc.md](windows_msvc.md),
[desktop_webgpu.md](desktop_webgpu.md), and [webgpu_dependencies.md](webgpu_dependencies.md).
