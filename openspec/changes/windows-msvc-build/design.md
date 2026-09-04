# Design: Windows MSVC build

## Context

See proposal.md — Why. Current state that shapes the approach (all verified on the user's machine, 2026-09-04):

- Windows currently builds only via MSYS2 CLANG64. `third_party/AnnoRefl/parser/parser.cmake` has a `WIN32 + Clang` branch that redirects `LIBCLANG_LIBRARY_PATH` to the MSYS2 `clang64/bin` and passes `--target=x86_64-w64-windows-gnu`, `-resource-dir`, and MSYS2 prefix include paths; the `WIN32` non-Clang fallback passes `--target=x86_64-w64-windows-gnu -stdlib=libstdc++` (wrong for MSVC — MSVC would fall into this branch).
- pip's `libclang` wheel bundles clang 18.x, too old to parse current MSVC STL headers. The VS "C++ Clang tools for Windows" component ships clang 22.1.3 at `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\libclang.dll`, with builtin headers at `...\Llvm\x64\lib\clang\22\include` (self-located, no `-resource-dir` needed). The VS Llvm install ships no Python bindings, so the pip `clang` package remains the bindings source; `clang.cindex` honors `LIBCLANG_LIBRARY_PATH` natively.
- The repo's engine CMake already has MSVC-aware branches (`engine/CMakeLists.txt`, `engine/Rhi/CMakeLists.txt` — `VULKAN_HPP_STORAGE_API`), and `engine/Framework/CMakeLists.txt` copies SDL3 next to the exes via `$<TARGET_FILE:SDL3::SDL3-shared>`, which works with any SDL3 shared import target.
- Root compile options are GCC/Clang-flavored (`-g -O0 -Wall -Wextra -Wpedantic -Weffc++` at root, `-w` in `third_party/CMakeLists.txt`) and will make cl.exe error out.
- Environment verified on the user's machine: VS2026 (`...\Visual Studio\18\Community`, MSVC 14.51 → `_MSC_VER` 1951, Windows SDK 10.0.26100.0), CMake 4.4.3, LunarG Vulkan SDK 1.4.313.2 (`VULKAN_SDK` set, loader in System32, `VK_LAYER_PATH` not set), Python 3.13.5 (miniconda), no vcpkg, no project `.venv` yet.

## Goals / Non-Goals

**Goals:**
- One CMake codebase: Linux (clang/ninja, unchanged) + Windows (MSVC, VS generator), with no MSYS2 references left anywhere.
- Reflection parser on Windows runs on the VS-bundled libclang with MSVC-target args; generated code compiles and the existing reflection/serialization tests pass.
- Fresh-machine install story is: VS2026 + two components, LunarG SDK, SDL3 official VC zip + `SDL3_ROOT`, Python `.venv` + `pip install -r requirements.txt`, optional Doxygen.

**Non-Goals:**
- No parity diff against old MSYS2-generated reflection output — compiling + tests passing is the acceptance bar (user decision).
- No vcpkg (user rejected it; SDL3 via official zip), no CI, no packaging/install rules, no changes to engine runtime behavior or reflection output semantics, no support for clang-cl toolset (it incidentally works via the WIN32 branch, but is not tested).

## Decisions

### D1: Visual Studio multi-config generator, `build/msvc` tree

Presets: one configure preset `msvc` (`generator: "Visual Studio 18 2026"`, `binaryDir: ${sourceDir}/build/msvc`, `Python3_EXECUTABLE: ${sourceDir}/.venv/Scripts/python.exe`), plus `msvc-debug` / `msvc-release` build presets (`"configuration": "Debug"` / `"Release"`) and matching test presets. The existing `debug` / `release` single-config presets stay — `linux-*` presets inherit them for `CMAKE_BUILD_TYPE`.

Consequences accepted: `CMAKE_BUILD_TYPE` is ignored by the VS generator; `CMAKE_RUNTIME_OUTPUT_DIRECTORY`/`CMAKE_LIBRARY_OUTPUT_DIRECTORY`/`CMAKE_ARCHIVE_OUTPUT_DIRECTORY` gain per-config subdirs (`build/msvc/bin/Debug/` etc.) — CMake appends the config name automatically for multi-config generators; docs and test run paths use them. The reflection parser's custom command stamping is config-independent and unaffected.

**Why VS generator over Ninja + cl.exe:** zero environment setup (no Developer PowerShell / vcvars requirement — CMake locates VS via registry/vswhere from any shell), native VS2026 IDE integration, and `CMAKE_GENERATOR_INSTANCE` hands the parser CMake the VS root directly for the libclang lookup. Ninja stays available on the machine but is unused for Windows MSVC builds.

**Alternatives considered:** Ninja + cl.exe (keeps current preset shape but requires launching from a dev shell — rejected as the primary flow); Ninja Multi-Config (same dev-shell problem); vcpkg toolchain-file mode (rejected with vcpkg entirely, see D4).

### D2: libclang from VS "C++ Clang tools", located in CMake

New `WIN32` branch in `anrorefl_libclang_extra_args()`:

1. Find the VS root: `${CMAKE_GENERATOR_INSTANCE}` (VS generator), falling back to deriving it from the `CMAKE_CXX_COMPILER` path (cl.exe at `VC\Tools\MSVC\<ver>\bin\Hostx64\x64\cl.exe` → up to the `VC` dir).
2. Check `VC\Tools\Llvm\x64\bin\libclang.dll`; on success `set(ENV{LIBCLANG_LIBRARY_PATH} ...)` and use the MSVC parse args (D3). On failure: `FATAL_ERROR` naming the "C++ Clang tools for Windows" component and how to add it via the VS Installer.
3. Keep the existing probe ordering: `anrorefl_libclang_extra_args()` runs before `ensure_parser_python()` inside `add_reflection_parser()` (the Linux change already established that the `import clang` probe must see the redirected `LIBCLANG_LIBRARY_PATH` first).

The non-Windows branch stays exactly as the Linux change left it (`-resource-dir` pinning). The old `WIN32 + Clang` MSYS2 branch and the GNU/MinGW fallback are deleted. A clang-cl toolset user would land in the same `WIN32` branch and get correct args as a side effect.

**Why VS Llvm over LLVM installer:** already installed with VS, version-matched to the VS STL (clang 22 parses the STL it ships with by design), one installer story ("install VS with these components"), zero extra packages. User decision.

### D3: MSVC parse arguments

For `WIN32`:

- `EXTRA_ARGS = --target=x86_64-pc-windows-msvc -fms-compatibility -fms-extensions -fmsc-version=${MSVC_VERSION}` (CMake's `MSVC_VERSION` equals `_MSC_VER`, e.g. 1951 on the verified machine).
- Include dirs: pass `CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES` (MSVC STL + Windows SDK ucrt/um/shared) as `-I` args, quoted to survive paths with spaces. Deterministic across CI/fresh machines; avoids relying on clang's registry-based MSVC detection.
- Keep `-MG -M -o ${CMAKE_BINARY_DIR}/parser_log.txt` (user intent: reflection should not care about unresolvable headers; the log output aids debugging) and `-xc++ -std=c++20 -ferror-limit=0`.
- Delete: `--target=x86_64-w64-windows-gnu`, `-stdlib=libstdc++`, `-resource-dir <msys2>`, `-I <msys2>/include/c++/v1`, `-I <msys2>/include`.
- `-DFLT_MAX -DFLT_MIN`: delete, then re-add only if MSVC STL parsing breaks without it (test decides; it was a MinGW-float.h-era workaround).
- `-fdelayed-template-parsing`: omit initially; add only if MSVC STL parsing produces template-instantiation errors (test decides).
- Fix `processor.py`: `config["args"].split()` → `shlex.split(..., posix=False)` so quoted `-I C:\Program Files\...` paths survive; quote include paths when assembling `REFLECTION_PARSER_ARGS` in CMake.

**Why explicit system includes:** clang's MSVC detection works via registry/env but silently degrades in odd shells (e.g. no INCLUDE env, non-standard VS layouts); CMake already computed the authoritative include list for the active toolchain, so passing it through removes an entire failure class.

### D4: SDL3 via official VC dev package + `SDL3_ROOT`

No vcpkg (user decision). Provisioning: download `SDL3-devel-<ver>-VC.zip` from the SDL GitHub releases, extract, set the environment variable **`SDL3_ROOT`** (exact case — CMake's `find_package` honors `<PackageName>_ROOT` and is case-sensitive; `SDL3_Root` would be ignored). `find_package(SDL3 CONFIG REQUIRED COMPONENTS SDL3-shared)` then resolves `SDL3Config.cmake` from `<root>/cmake/` with zero CMake changes, and the Framework POST_BUILD copy keeps placing SDL3.dll next to the exes (no runtime PATH for SDL3).

Vulkan stays on the LunarG SDK (>= 1.4.x, 1.4.313.2 verified): headers, loader, `glslangValidator.exe` (FindVulkan `glslangValidator` component), and validation layers in one consistent version set. `VULKAN_SDK` is auto-set by the SDK installer; `VK_LAYER_PATH` is not — docs mention it as optional for Debug validation layers (engine skips gracefully when missing).

**Why not vcpkg for everything:** vcpkg's `glslang[tools]` does ship `glslangValidator.exe`, but FindVulkan would not find it without hardcoding `Vulkan_GLSLANG_VALIDATOR_EXECUTABLE`; validation layers would need manual `VK_LAYER_PATH` wiring into `installed\x64-windows\bin`; ports version independently (the repo already hit a "SDK too old" failure on Linux); the validationlayers port is a heavy build; and the SDK is already installed and working.

### D5: Compiler-flag split for MSVC

Root `CMakeLists.txt` debug/release options and `third_party/CMakeLists.txt`'s `-w` become compiler-guarded generator expressions, e.g.:

```cmake
add_compile_options(
    $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU,Clang>>:-g -O0 -Wall -Wextra -Wpedantic -Weffc++>
    $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:MSVC>>:/Zi /Od /W4>
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU,Clang>>:-O3>
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/O2>
)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>)
```

`/Zc:__cplusplus` is mandatory: MSVC otherwise reports `__cplusplus` as 199711L, which breaks C++20-feature gating across the engine and third-party headers. Linux (Clang) behavior is unchanged by these expressions.

### D6: Documentation replacement

- New `docs/build_instructions/windows_msvc.md`: dependency table (VS2026 with Desktop C++ workload + "C++ Clang tools for Windows"; Vulkan SDK >= 1.4.x; SDL3 zip + `SDL3_ROOT`; Python + `.venv` + `pip install -r third_party/AnnoRefl/parser/requirements.txt`; optional Doxygen), preset usage (`cmake --preset msvc`, `--build --preset msvc-debug`, `ctest --preset msvc-debug`), per-config output paths under `build/msvc/bin/<Config>/`, runtime notes (SDL3.dll copied automatically; `VK_LAYER_PATH` optional), and the `CMakeUserPresets.json` override pattern for a non-standard Python.
- Delete `docs/build_instructions/windows_msys2_clang64.md` and the `msys2_path.txt` caching convention; update `AGENTS.md` (Windows section points at the MSVC doc; MSYS2 mention removed) and `.vscode/settings.json` guidance.

## Risks / Trade-offs

- **MSVC STL parse edge cases** (libclang on VS2026 STL) → Mitigation: clang 22 ships with VS2026 and is tested against that STL; residual issues handled by the D3 fallback flags (`-fdelayed-template-parsing`, `-DFLT_MAX -DFLT_MIN`), decided empirically by the build+test gate.
- **`-fmsc-version` skew** on other VS versions (clang clamps with a warning) → Mitigation: derived from `MSVC_VERSION`, so it always matches the active toolchain; older VS versions may warn but VS2026 is the verified target.
- **Silent parse degradation** (`-MG -M` tolerates missing headers, so broken setups can still generate wrong reflection code) → accepted trade-off (user decision); the reflection/serialization ctest suite is the semantic backstop.
- **MSVC CRT mismatch with vendored third-party libs** (libktx's astc-encoder forces a static MSVC runtime) → Mitigation: surface at first build; fix inside `third_party` if it trips (code-level, may spawn follow-up tasks).
- **Multi-config path changes break habits/scripts** (exes now under `build/msvc/bin/Debug/`) → Mitigation: documented prominently; test presets carry `"configuration"` so ctest needs no manual flags.
- **`linux-build-support` change declares "Windows parser behavior unchanged"** → this change supersedes it; that in-flight delta's Windows scenario must be reworded to reference the MSVC branch instead of MSYS2 (tracked as a task).
- **Stray MSYS2 references** (docs, VS Code settings, agent configs) → Mitigation: a grep sweep task (`MSYS2|msys64|CLANG64|x86_64-w64-windows-gnu|libstdc++`) with zero hits as its completion check.

## Migration Plan

1. Rewrite `parser.cmake` (D2, D3) + `processor.py` shlex fix.
2. Split compile flags (D5).
3. Add MSVC presets (D1); configure with `cmake --preset msvc` on VS2026 and fix fallout until configure is clean.
4. Full `cmake --build --preset msvc-debug` + `ctest --preset msvc-debug` pass (parser acceptance = reflection/serialization tests green).
5. `cmake --build --preset msvc-release` smoke build.
6. Docs replacement (D6) + MSYS2-reference sweep.
7. Update the in-flight `linux-build-support` artifacts' Windows scenario wording.

Rollback: confined to build scripts, presets, parser tooling, and docs; reverting restores the MSYS2 flow with no engine code impact.

## Open Questions

- Exact SDL3 version to pin in docs (any current `3.x.y` VC package; pin at doc-writing time).
- Whether `-fdelayed-template-parsing` / `-DFLT_MAX -DFLT_MIN` are needed on MSVC — resolved empirically in task 3.x; if either is required, keep it with a comment explaining why (no spec change needed).
- VS2022 (minimum supported) is untestable on the user's machine (only VS2026 installed); MSVC_VERSION-derived args should hold, but a VS2022 smoke test is a follow-up if a machine becomes available.
