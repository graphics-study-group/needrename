# Proposal: Linux build support

## Why

The project currently builds and runs only on Windows via the MSYS2 CLANG64 toolchain. We want Linux (WSL2) to be a first-class build and run platform with the same workflow, so development is not locked to Windows. The reflection parser's hardcoded Windows venv assumptions (`Lib/site-packages`, `parser_env` prefix guard) actively block this: on Linux the venv is never detected as ready and the parser environment cannot be provisioned or verified.

## What Changes

- **Python environment: user-managed instead of auto-created** (**BREAKING** for parser provisioning)
  - Remove automatic venv creation, the `VIRTUAL_ENV`/`Python3_FIND_VIRTUALENV` hack, the Windows-only `Lib/site-packages/clang` check, and the `ANROREFL_PARSER_ENV_DIR` cache variable.
  - Use `find_package(Python3 REQUIRED COMPONENTS Interpreter)`; the interpreter path is supplied by the user via preset cache variables (`Python3_EXECUTABLE`, e.g. their own `.venv`), ideally through a gitignored `CMakeUserPresets.json`.
  - At configure time run an import probe (`import clang, mako`); fail fast with an actionable message when missing.
  - Remove the `sys.prefix` "parser_env" guard from `parser_main.py`.
- **Parser libclang arguments generalized** (no behavior change on Windows)
  - Extract the `EXTRA_ARGS` construction into a reusable function. Windows keeps today's exact behavior (target triple, `-resource-dir`, libc++ include paths, `LIBCLANG_LIBRARY_PATH`).
  - Linux: append `-resource-dir` from `clang -print-resource-dir` (silently skip on failure), keep GCC toolchain detection for C++ standard library headers.
  - Keep `-DFLT_MAX -DFLT_MIN` on all platforms.
- **Linux runtime shared-library loading**
  - Set `CMAKE_BUILD_RPATH_USE_ORIGIN ON` so executables in the unified `bin/` directory find sibling `.so` libraries (`$ORIGIN`), matching Windows' DLL-in-exe-dir behavior.
- **Linux documentation**
  - Add `docs/build_instructions/linux.md`: dependency table + install commands (Ubuntu 24.04 verified), manual SDL3 build steps, WSL2 graphics notes (WSLg/gfxstream + lavapipe), validation layers.
  - Update `README.md` dependency table and `AGENTS.md` environment-variable guidance to be platform-aware.
- **Dependency discovery confirmed on Ubuntu 24.04** (no code change needed)
  - `find_package(Vulkan REQUIRED COMPONENTS glslangValidator)` works via CMake's built-in FindVulkan with `glslang-tools` installed.
  - `find_package(SDL3 CONFIG REQUIRED COMPONENTS SDL3-shared)` works with a manually built SDL3 3.5.0 at `/usr/local`.

## Capabilities

### New Capabilities
- `linux-build-support`: cross-platform build and run behavior — Linux is a supported build platform, reflection parser Python provisioning is user-managed and verified at configure time, Linux binaries resolve sibling shared libraries at runtime, and platform-specific build documentation exists.

### Modified Capabilities
- None. Existing specs describe engine behavior that does not change; the reflection-module codegen target names and runtime behavior are untouched.

## Impact

- **Build system**: `CMakeLists.txt` (root), `CMakePresets.json`, `third_party/AnnoRefl/parser/parser.cmake`, `third_party/AnnoRefl/parser/parser_main.py`, new `CMakeUserPresets.json` (gitignored).
- **Dependencies**: removes `ANROREFL_PARSER_ENV_DIR`; Linux requires clang, ninja, `libvulkan-dev`, `glslang-tools`, SDL3 (source-built on Ubuntu 24.04), `uuid-dev`, `python3-venv`.
- **Docs**: `docs/build_instructions/linux.md` (new), `README.md`, `AGENTS.md`.
- **Testing**: ctest on Linux (headless tests; windowed tests need WSLg).
- **Windows**: parser behavior unchanged; verified via generated-code diff before merging.
