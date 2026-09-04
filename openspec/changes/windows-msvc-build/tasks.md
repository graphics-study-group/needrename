# Tasks: windows-msvc-build

## 1. Parser: MSVC libclang branch

- [ ] 1.1 Rewrite the `WIN32` branch of `anrorefl_libclang_extra_args()` in `third_party/AnnoRefl/parser/parser.cmake`: locate the VS root via `CMAKE_GENERATOR_INSTANCE` (fallback: derive from `CMAKE_CXX_COMPILER`), probe `VC\Tools\Llvm\x64\bin\libclang.dll`, set `LIBCLANG_LIBRARY_PATH`, and `FATAL_ERROR` with an actionable message naming the "C++ Clang tools for Windows" component when absent; verify a fresh `cmake --preset msvc` on the user's machine shows `LIBCLANG_LIBRARY_PATH` pointing at the VS Llvm bin dir
- [ ] 1.2 Replace the MSVC-branch `EXTRA_ARGS` with `--target=x86_64-pc-windows-msvc -fms-compatibility -fms-extensions -fmsc-version=${MSVC_VERSION}` plus quoted `-I` flags from `CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES`, and delete the MSYS2/GNU args (`x86_64-w64-windows-gnu`, `-stdlib=libstdc++`, `-resource-dir`, MSYS2 prefix `-I` paths); verify the configure debug log shows the new args and no MSYS2 paths
- [ ] 1.3 Delete the old `WIN32 + Clang` MSYS2 branch and the GNU/MinGW fallback, keeping the non-Windows branch byte-identical; verify `grep` finds no `x86_64-w64-windows-gnu|libstdc++|msys` hits in `parser.cmake`
- [ ] 1.4 Fix argument splitting in `third_party/AnnoRefl/parser/processor.py` (`config["args"].split()` → `shlex.split(..., posix=False)`) and quote include paths when assembling `REFLECTION_PARSER_ARGS`; verify a reflection generation target (e.g. `meta_core`) runs with the default VS/Windows SDK paths containing spaces
- [ ] 1.5 Run the parser on VS2026 and verify generated reflection code compiles and the reflection/serialization tests pass; if MSVC STL parsing fails, try re-adding `-DFLT_MAX -DFLT_MIN` or adding `-fdelayed-template-parsing` one at a time and keep whichever makes it pass with a comment explaining why

## 2. MSVC compiler flags

- [ ] 2.1 Split root `CMakeLists.txt` debug/release compile options into compiler-guarded generator expressions (`GNU,Clang` keep `-g -O0 -Wall -Wextra -Wpedantic -Weffc++` / `-O3`; MSVC gets `/Zi /Od /W4` / `/O2`) and add `/Zc:__cplusplus` for MSVC; verify a Linux configure still shows the original flags and an MSVC configure shows the `/`-style flags
- [ ] 2.2 Replace `-w` in `third_party/CMakeLists.txt` with `$<$<CXX_COMPILER_ID:GNU,Clang>:-w>$<$<CXX_COMPILER_ID:MSVC>:/w>`; verify third-party targets compile under both toolchains without warning floods

## 3. Presets: VS multi-config generator

- [ ] 3.1 Add the `msvc` configure preset (`Visual Studio 18 2026`, `build/msvc`, `Python3_EXECUTABLE: ${sourceDir}/.venv/Scripts/python.exe`) and `msvc-debug`/`msvc-release` build + test presets with `"configuration"` set; verify `cmake --preset msvc` configures cleanly from a plain PowerShell (no dev shell)
- [ ] 3.2 Keep the existing `debug`/`release` presets as the Linux base and verify `cmake --preset linux-debug` still configures on Linux (no Windows-only changes leaked into shared presets)

## 4. Build and test on VS2026

- [x] 4.1 User-side deps: create `.venv` and `pip install -r third_party/AnnoRefl/parser/requirements.txt`, download SDL3-devel VC zip and set `SDL3_ROOT`; verify configure resolves SDL3 (`find_package(SDL3 CONFIG REQUIRED COMPONENTS SDL3-shared)`) with no cache overrides
- [ ] 4.2 Run `cmake --build --preset msvc-debug` and verify all targets build with cl.exe (fix any MSVC-specific errors surfacing in third-party or engine code, e.g. libktx CRT mismatch)
- [ ] 4.3 Run `ctest --preset msvc-debug` and verify the suite passes, including reflection/serialization tests
- [ ] 4.4 Smoke-build `cmake --build --preset msvc-release` and verify it completes

## 5. Documentation and MSYS2 removal

- [ ] 5.1 Write `docs/build_instructions/windows_msvc.md` (VS2026 + Desktop C++ + C++ Clang tools components, Vulkan SDK >= 1.4.x, SDL3 zip + exact `SDL3_ROOT` env var, `.venv` + pip requirements, `CMakeUserPresets.json` override pattern, preset commands, per-config output paths under `build/msvc/bin/<Config>/`, optional `VK_LAYER_PATH` note, optional Doxygen) and verify every listed command runs on the user's machine
- [ ] 5.2 Delete `docs/build_instructions/windows_msys2_clang64.md` and the `msys2_path.txt` convention; update `AGENTS.md` (Windows → MSVC doc, remove MSYS2 guidance) and `.vscode/settings.json`; verify `grep -ri "msys2|msys64|clang64|x86_64-w64-windows-gnu"` (excluding third_party and archive) returns no hits
- [ ] 5.3 Update the in-flight `linux-build-support` change: reword its "Windows parser behavior unchanged" spec scenario and parity-verification tasks to reference the MSVC Windows flow instead of MSYS2; verify that change still validates
