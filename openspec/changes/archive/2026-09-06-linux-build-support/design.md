# Design: Linux build support

## Context

See proposal.md — Why. Current state that shapes the approach:

- The reflection parser (`third_party/AnnoRefl/parser/parser.cmake`) auto-creates a Python venv, forces `Python3_FIND_VIRTUALENV ONLY`, checks readiness via a Windows-only path (`${PARSER_ENV_DIR}/Lib/site-packages/clang`), and `parser_main.py` refuses to run unless `sys.prefix` ends in `parser_env`. None of this generalizes to Linux.
- `EXTRA_ARGS` (parser.cmake:117-156) is a Windows/MSYS2-specific compensation for pip-bundled libclang; the non-WIN32 branch is empty.
- Executables and shared libraries all land in one `bin/` directory; Windows finds DLLs there by default, Linux does not.
- Verified on Ubuntu 24.04 WSL2: `find_package(Vulkan REQUIRED COMPONENTS glslangValidator)` works via CMake's built-in FindVulkan (glslang-tools installed); `find_package(SDL3 CONFIG REQUIRED COMPONENTS SDL3-shared)` works with manually built SDL3 3.5.0 at `/usr/local`. No change needed to either call.

## Goals / Non-Goals

**Goals:**
- One CMake codebase that configures on both Windows (MSYS2 CLANG64) and Linux (clang/ninja) with zero per-platform CMake forks.
- Parser Python provisioning becomes user-managed and platform-neutral, with a configure-time fail-fast probe.
- Linux binaries run from the build tree without environment workarounds.

**Non-Goals:**
- No CI setup, no packaging/install rules, no macOS support, no vendoring of SDL3 (deferred; see Open Questions).
- No changes to engine runtime behavior or reflection output semantics.

## Decisions

### D1: User-managed Python interpreter, probed at configure time
Replace auto-venv with `find_package(Python3 REQUIRED COMPONENTS Interpreter)`. The Linux shared presets set `Python3_EXECUTABLE` to `${sourceDir}/.venv/bin/python` (a repo-local `.venv` convention). A user with a non-standard venv location overrides it via a gitignored `CMakeUserPresets.json` that inherits the shared preset.

- The probe runs once: `execute_process(COMMAND ${Python3_EXECUTABLE} -c "import clang, mako")`. On failure, `FATAL_ERROR` with the exact pip command (`pip install -r third_party/AnnoRefl/parser/requirements.txt`).
- Probe placement: inside the parser setup function (called from `add_reflection_parser`), not at parser.cmake top-level, because on Windows it must run after `LIBCLANG_LIBRARY_PATH` is set (the probe imports `clang`; Windows loading of the MSYS2 libclang.dll depends on that env var).
- `parser_main.py`'s `sys.prefix[-10:] != 'parser_env'` guard is deleted; with user-managed envs the check is wrong on every platform.
- Remove `ANROREFL_PARSER_ENV_DIR` from root CMakeLists and AGENTS.md.

**Alternatives considered:** keep auto-venv but fix the path for Linux (adds cross-platform path juggling, still surprises users by mutating the build dir); system `python3-clang` package (binds to system Python, breaks the user-managed venv model).

### D2: Generalize EXTRA_ARGS construction, Windows behavior unchanged
Extract the WIN32 block into a function `anrorefl_libclang_extra_args()`:

- WIN32 + Clang: identical to today (libclang.dll probe, `--target=x86_64-w64-windows-gnu`, `-resource-dir`, libc++/mingw `-I` paths).
- WIN32 non-Clang: unchanged fallback (`-stdlib=libstdc++`).
- Linux: append `-resource-dir` from `${CMAKE_CXX_COMPILER} -print-resource-dir` (reuse the existing `_RC EQUAL 0` guard pattern; skip silently on failure or if the compiler is GCC). C++ standard library headers are left to clang's GCC auto-detection; this mirrors Windows' intent — "make the parser's libclang see the same builtin headers as the real toolchain".
- `-DFLT_MAX -DFLT_MIN` stays global (existing float.h workaround; reevaluate only if the parity diff proves it unneeded).

**Alternatives considered:** empty args on Linux (relies on pip libclang defaults; weakest coupling to the project toolchain, and `-MG -M` turns header-resolution failures silent); system libclang via `LIBCLANG_LIBRARY_PATH` + `libclang-18-dev` (strongest consistency but adds a distro-version-coupled system dependency and breaks the pip-only provisioning story on Linux).

### D3: `CMAKE_BUILD_RPATH_USE_ORIGIN ON` in root CMakeLists
Simplest fix that mirrors Windows DLL-in-exe-dir behavior. CMake already emits build RPATHs for linked libraries; this rewrites them relative to `$ORIGIN`, so `bin/` stays relocatable and runtime-loaded modules (e.g. editor plugin loading) resolve sibling `.so` files.

**Alternatives considered:** explicit per-target `BUILD_RPATH` (more code, no benefit at this scale); `CMAKE_SKIP_BUILD_RPATH` (breaks everything); full install RPATH machinery (out of scope — no install rules).

### D4: Vulkan dependency via manually installed LunarG SDK
Linux does NOT use apt-provided Vulkan packages. The LunarG Vulkan SDK (>= 1.4.x) is downloaded manually from vulkan.lunarg.com and installed under `~/software/VulkanSDK/`, with `source <sdk>/setup-env.sh` persisted in `~/.bashrc`. Verified with SDK 1.4.357.1:

- `x86_64/bin/` provides `glslangValidator` (and `glslc`) — replaces apt `glslang-tools`.
- `x86_64/include/vulkan/` provides vulkan.hpp 1.4.357 which contains `vk::detail::resultCheck` (the API the engine requires and Ubuntu's system 1.3.275 headers lack — the cause of the 50+ build errors).
- `x86_64/lib/libVkLayer_khronos_validation.so` + `share/vulkan/explicit_layer.d/*.json` provide validation layers — replaces apt `vulkan-validationlayers`. `setup-env.sh` exports `VK_ADD_LAYER_PATH` (and unsets `VK_LAYER_PATH`).
- `x86_64/lib/VulkanLoader/lib/libvulkan.so` provides the loader; `setup-env.sh` exports `LD_LIBRARY_PATH` and `CMAKE_PREFIX_PATH` so CMake's built-in FindVulkan module resolves include/lib/validator from the SDK with **zero CMake changes**.

No CMake changes for discovery: `find_package(Vulkan REQUIRED COMPONENTS glslangValidator)` resolves via FindVulkan module mode once the SDK env is active. System packages `libvulkan1` (mesa dependency, runtime fallback loader) and `mesa-vulkan-drivers` (WSLg gfxstream ICD) stay installed. Doc note: non-interactive shells (CI/scripts) must source setup-env.sh explicitly since `~/.bashrc` only runs in interactive shells.

### D5: Documentation layout
- New `docs/build_instructions/linux.md`: dependency table with `apt install` commands (Ubuntu 24.04), SDL3 source build steps (needs `libwayland-dev`, `libxkbcommon-dev` for WSLg/Wayland), WSL2 Vulkan notes (gfxstream vs lavapipe ICDs, validation layers, `DISPLAY`/`WAYLAND_DISPLAY`), and the `CMakeUserPresets.json` Python-example.
- `README.md`: add Linux dependency table next to the MSYS2 one.
- `AGENTS.md`: make the MSYS2 environment-variable section explicitly Windows-scoped.

## Risks / Trade-offs

- **Silent parse degradation** (`-MG -M` tolerates missing headers, so a broken libclang setup produces wrong reflection code without errors) → Mitigation: parity verification task — diff Linux-generated `__generated__` output against Windows output for `meta_core` before merging.
- **pip libclang version drift vs system headers** (pip clang ≈ 20 vs clang 18 toolchain / GCC 13 libstdc++) → Mitigation: `-resource-dir` pins builtin headers to the project toolchain; GCC detection is mature. Residual risk covered by the parity diff.
- **SDL3 not in Ubuntu 24.04 repos** → Mitigation: documented source build; vendoring kept as a future option (Open Questions). Ubuntu 24.10+ ships `libsdl3-dev`, so this pain shrinks over time.
- **Vulkan SDK manual install is machine-local state** → Mitigation: version pinned in docs (>= 1.4.x, verified 1.4.357.1); non-interactive shells (CI) must source setup-env.sh explicitly; CMake fails with a clear error if VULKAN_SDK is missing.
- **Clang version skew Windows (22) vs Linux (18)** could theoretically produce different metadata → Mitigation: parity diff catches it; type spellings are version-stable in practice.
- **User forgets `Python3_EXECUTABLE` or their env lacks requirements** → Mitigation: FATAL_ERROR with actionable pip instructions; Linux instructions show the `CMakeUserPresets.json` snippet.

## Migration Plan

1. Implement parser.cmake changes + parser_main.py guard removal (D1, D2).
2. Add RPATH line (D3).
3. Generate `__generated__` output on Windows with the new parser code and diff against git; confirm byte-identical (regression gate for D1/D2 Windows behavior).
4. Generate `__generated__` output on Linux and diff against the Windows output (parity gate).
5. Write docs (D5), update README/AGENTS.
6. Full Linux build + ctest pass (headless; windowed via WSLg if display available).

Rollback: the change is confined to build scripts + docs + one guard removal in a Python script; reverting restores Windows-only behavior with no engine code impact.

## Open Questions

- **SDL3 provisioning on Linux long-term**: keep documented source builds, or vendor SDL3 as a git submodule (consistent with `third_party/`)? Deferred — documentation suffices for Ubuntu 24.04 today; can revisit as a follow-up change without touching specs.
- Whether `-DFLT_MAX -DFLT_MIN` is still needed on Linux: answered empirically by the parity diff; if the diff shows it unnecessary it can be scoped to WIN32 in a follow-up.
