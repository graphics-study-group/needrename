# Tasks: linux-build-support

## 1. Parser: user-managed Python environment

- [x] 1.1 Rework `third_party/AnnoRefl/parser/parser.cmake`: remove `create_python_venv`, `setup_python_environment` venv creation/`VIRTUAL_ENV`/`Python3_FIND_VIRTUALENV` logic, and the `Lib/site-packages/clang` readiness check; replace with `find_package(Python3 REQUIRED COMPONENTS Interpreter)` + a configure-time probe (`import clang, mako`) and verify a fresh configure succeeds with a user-provided interpreter
- [x] 1.2 Remove `ANROREFL_PARSER_ENV_DIR` from root `CMakeLists.txt` and AGENTS.md and verify no references remain (`grep -r ANROREFL_PARSER_ENV_DIR`)
- [x] 1.3 Delete the `sys.prefix[-10:] != 'parser_env'` guard from `parser_main.py` and verify the parser runs from any interpreter path
- [x] 1.4 Add a `CMakeUserPresets.json` example (gitignored, inheriting shared presets, setting `Python3_EXECUTABLE`) to the docs; verify it is not tracked by git

## 2. Parser: generalized libclang arguments

- [x] 2.1 Extract the WIN32 `EXTRA_ARGS` block into a function; keep the Windows MSYS2 path byte-identical and verify configure logs show the same parser args as before on Windows
- [x] 2.2 Add the Linux branch: append `-resource-dir` from `${CMAKE_CXX_COMPILER} -print-resource-dir` with the existing `_RC EQUAL 0` guard, keep `-DFLT_MAX -DFLT_MIN` global, and verify a Linux configure passes the args through to the parser invocation

## 3. RPATH

- [x] 3.1 Add `set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)` to root `CMakeLists.txt` and verify a Linux test executable in `build/debug/bin/` runs without `LD_LIBRARY_PATH` setup

## 4. Parity verification

- [ ] 4.1 On Windows with the new parser code, regenerate `__generated__` output (e.g. `meta_core`) and diff against the committed output; verify byte-identical
- [ ] 4.2 On Linux, generate the same `__generated__` output and diff against the Windows output; verify equivalent (residual differences, if any, documented and approved)

## 5. Linux build and tests

- [x] 5.1 Verify the Vulkan SDK env is active in an interactive shell (`source <sdk>/setup-env.sh` persisted in `~/.bashrc`; `VULKAN_SDK` and `glslangValidator` on PATH) and verify `cmake --preset linux-debug-local` configures cleanly on Ubuntu 24.04 with the SDK headers/loader/validator resolved
- [x] 5.2 Build all targets on Linux and verify `cmake --build --preset debug` completes with no errors
- [ ] 5.3 Run `ctest --preset debug` on Linux and verify headless tests pass; windowed tests pass under WSLg (or are reported with a documented display requirement)

## 6. Documentation

- [x] 6.1 Write `docs/build_instructions/linux.md` (Ubuntu 24.04 dependency table + `apt install` commands, LunarG Vulkan SDK >= 1.4.x manual install from vulkan.lunarg.com with `setup-env.sh` persisted in `~/.bashrc` and the non-interactive-shell caveat, SDL3 source build steps, WSL2 Vulkan ICDs/validation layers/display notes, `CMakeUserPresets.json` Python example) and verify every listed command runs on a clean install
- [x] 6.2 Update `README.md` with a Linux dependency table alongside the MSYS2 table and verify links/format
- [x] 6.3 Scope the `AGENTS.md` MSYS2 environment-variable section as Windows-only and add a Linux pointer; verify docs cross-reference correctly
