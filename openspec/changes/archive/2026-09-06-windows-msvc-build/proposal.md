# Proposal: Windows MSVC build

## Why

The Windows build currently requires the MSYS2 CLANG64 toolchain, which adds a second environment (compilers, Python, packages, PATH juggling) on top of Visual Studio. The user wants Windows to build with plain MSVC (Visual Studio), with MSYS2 dropped entirely. The reflection parser is the main blocker: pip's bundled libclang is too old to parse current MSVC STL headers, so the parser must use the VS-bundled libclang (clang 22, verified present on the user's machine).

## What Changes

- **Windows MSYS2 support removed entirely** (**BREAKING**)
  - Delete `docs/build_instructions/windows_msys2_clang64.md`, the `msys2_path.txt` caching convention, and all MSYS2-specific logic in `third_party/AnnoRefl/parser/parser.cmake` (the `WIN32 + Clang` MSYS2 branch and the GNU/MinGW fallback args).
  - `AGENTS.md` platform guidance and VS Code settings updated to MSVC.
- **MSVC presets with Visual Studio multi-config generator**
  - New `msvc` configure preset (`Visual Studio 18 2026` generator, `build/msvc` binary dir, `Python3_EXECUTABLE` pinned to `${sourceDir}/.venv/Scripts/python.exe`), plus `msvc-debug` / `msvc-release` build and test presets.
  - Existing single-config `debug` / `release` presets remain as the base for the Linux presets.
- **Reflection parser: MSVC branch**
  - Locate the VS-bundled libclang (`VC\Tools\Llvm\x64\bin\libclang.dll`) and redirect it via `LIBCLANG_LIBRARY_PATH`; fail configure with an actionable message when the "C++ Clang tools for Windows" component is missing.
  - Replace the MSYS2 parse arguments with MSVC-target args (`--target=x86_64-pc-windows-msvc`, `-fms-compatibility`, `-fms-extensions`, `-fmsc-version` derived from `MSVC_VERSION`) and MSVC system include dirs; delete `-stdlib=libstdc++`, `-resource-dir`, and the MSYS2 `-I` prefix paths. Keep `-MG -M -o parser_log.txt` (reflection tolerates missing headers; the log aids debugging).
  - Fix argument splitting in `processor.py` so include paths containing spaces survive (shlex).
- **MSVC compiler-flag compatibility**
  - Split GCC/Clang-style compile options (`-g -O0 -Wall -Wextra -Wpedantic -Weffc++`, `-w`) by compiler via generator expressions; add MSVC equivalents (`/Zi /Od /W4`, `/O2`, `/w`) and `/Zc:__cplusplus`.
- **SDL3 provisioning without vcpkg**
  - Official `SDL3-devel-x.y.zz-VC.zip` from GitHub releases + `SDL3_ROOT` environment variable; `find_package(SDL3 CONFIG)` unchanged. Vulkan stays on the LunarG SDK (>= 1.4.x, already installed and verified).
- **Validation criteria**
  - Success = configure + build + existing ctest suite pass on VS2026. No byte-level parity against old MSYS2-generated reflection code is required.

## Capabilities

### New Capabilities
- `windows-msvc-build`: Windows builds with the MSVC toolchain via the Visual Studio generator, MSYS2 is fully removed, the reflection parser uses the VS-bundled libclang with MSVC-target parse arguments, and Windows build documentation covers VS components, SDL3, Python venv, and runtime environment.

### Modified Capabilities
- None. Existing specs describe engine behavior that does not change; reflection output semantics are unchanged.

## Impact

- **Build system**: `CMakePresets.json`, root `CMakeLists.txt`, `third_party/CMakeLists.txt`, `third_party/AnnoRefl/parser/parser.cmake`, `third_party/AnnoRefl/parser/processor.py`.
- **Docs**: `docs/build_instructions/windows_msvc.md` (new), `docs/build_instructions/windows_msys2_clang64.md` (deleted), `AGENTS.md`, `.vscode/settings.json`.
- **Dependencies**: MSYS2 toolchain/packages no longer required. Required on the user's machine: VS2022+ (verified VS2026) with Desktop C++ workload + "C++ Clang tools for Windows", CMake 4.x, LunarG Vulkan SDK >= 1.4.x, SDL3 official VC dev package, any Python 3 (verified 3.13).
- **Cross-change interaction**: the in-flight `linux-build-support` change declares "Windows parser behavior unchanged (target triple, resource dir, C++ include paths)" — that scenario is superseded by this change and must be reworded when that change lands (or now, in its delta).
- **Testing**: full `cmake --build` + `ctest` on VS2026 (`build/msvc`), including reflection/serialization tests as the semantic gate for the parser changes.
