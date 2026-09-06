# Tasks: windows-msvc-build

## 1. Parser: MSVC libclang branch

- [x] 1.1 Rewrite the `WIN32` branch of `anrorefl_libclang_extra_args()` in `third_party/AnnoRefl/parser/parser.cmake`: locate the VS root via `CMAKE_GENERATOR_INSTANCE` (fallback: derive from `CMAKE_CXX_COMPILER`), probe `VC\Tools\Llvm\x64\bin\libclang.dll`, set `LIBCLANG_LIBRARY_PATH`, and `FATAL_ERROR` with an actionable message naming the "C++ Clang tools for Windows" component when absent; verify a fresh `cmake --preset msvc` on the user's machine shows `LIBCLANG_LIBRARY_PATH` pointing at the VS Llvm bin dir
- [x] 1.2 Replace the MSVC-branch `EXTRA_ARGS` with `--target=x86_64-pc-windows-msvc -fms-compatibility -fms-extensions -fmsc-version=${MSVC_VERSION}` and delete the MSYS2/GNU args (`x86_64-w64-windows-gnu`, `-stdlib=libstdc++`, `-resource-dir`, MSYS2 prefix `-I` paths); verify a reflection generation target runs with the new args and no MSYS2 paths
- [x] 1.3 Delete the old `WIN32 + Clang` MSYS2 branch and the GNU/MinGW fallback, keeping the non-Windows branch byte-identical; verify no MSYS2/GNU hits remain in `parser.cmake` (only the Linux-branch `libstdc++` comment and `-resource-dir` pinning remain by design)
- [x] 1.4 Fix argument splitting in `third_party/AnnoRefl/parser/processor.py` (`config["args"].split()` ? `shlex.split(..., posix=False)`); verify a reflection generation target (e.g. `meta_core`) runs during the MSVC build
- [x] 1.5 Run the parser on VS2026 and verify generated reflection code compiles and the reflection/serialization tests pass; `-DFLT_MAX -DFLT_MIN` and `-fdelayed-template-parsing` were not needed (MSVC STL parsed cleanly with clang 22)

## 2. MSVC compiler flags

- [x] 2.1 Split root `CMakeLists.txt` debug/release compile options into compiler-guarded generator expressions (`GNU,Clang` keep `-g -O0 -Wall -Wextra -Wpedantic -Weffc++` / `-O3`; MSVC gets `/Zi /Od /W4` / `/O2`) and add `/Zc:__cplusplus` for MSVC; verified an MSVC configure uses the `/`-style flags (Linux flags preserved by construction; see 3.2)
- [x] 2.2 Replace `-w` in `third_party/CMakeLists.txt` with `$<$<CXX_COMPILER_ID:GNU,Clang>:-w>$<$<CXX_COMPILER_ID:MSVC>:/w>`; verified third-party targets compile under MSVC

## 3. Presets: VS multi-config generator

- [x] 3.1 Add the `msvc` configure preset (`Visual Studio 18 2026`, `build/msvc`, `Python3_EXECUTABLE: ${sourceDir}/.venv/Scripts/python.exe`) and `msvc-debug`/`msvc-release` build + test presets with `"configuration"` set; verified `cmake --preset msvc` configures cleanly from a plain PowerShell (no dev shell)
- [ ] 3.2 Linux presets (`linux-debug` / `linux-release`, self-contained Ninja presets) are unaffected by the Windows changes and compile-flag expressions preserve the Linux flags; final verification `cmake --preset linux-debug` on Linux requires a Linux environment (see note in summary)

## 4. Build and test on VS2026

- [x] 4.1 User-side deps: create `.venv` and `pip install -r third_party/AnnoRefl/parser/requirements.txt`, download SDL3-devel VC zip and set `SDL3_ROOT`; verify configure resolves SDL3 (`find_package(SDL3 CONFIG REQUIRED COMPONENTS SDL3-shared)`) with no cache overrides
- [x] 4.2 Run `cmake --build --preset msvc-debug` and verify all targets build with cl.exe (fixed MSVC-specific engine errors: missing `<string>`/`<stdexcept>`/`<limits>` includes, `cxxabi.h`, `__builtin_unreachable`, class/struct forward-decl mangling mismatches, move-only container semantics)
- [x] 4.3 Run `ctest --preset msvc-debug` and verify the suite passes (54/54), including reflection/serialization tests
- [x] 4.4 Smoke-build `cmake --build --preset msvc-release` and verify it completes

## 5. Documentation and MSYS2 removal

- [x] 5.1 Write `docs/build_instructions/windows_msvc.md` (VS2026 + Desktop C++ + C++ Clang tools components, Vulkan SDK >= 1.4.x, SDL3 zip + exact `SDL3_ROOT` env var, `.venv` + pip requirements, `CMakeUserPresets.json` override pattern, preset commands, per-config output paths under `build/msvc/bin/<Config>/`, optional `VK_LAYER_PATH` note, optional Doxygen); every preset command verified on the user's machine
- [x] 5.2 Delete `docs/build_instructions/windows_msys2_clang64.md`; update `AGENTS.md`, `README.md`, `docs/README_CN.md`, `docs/build_instructions/linux.md`, `engine/Framework/CMakeLists.txt` comment, and `.vscode/settings.json` (removed the Ninja-generator default); verified no `msys2|msys64|clang64|x86_64-w64-windows-gnu` hits remain outside `third_party`, `archive`, and OpenSpec change artifacts that document the migration
- [x] 5.3 Update the in-flight `linux-build-support` change: reword its "Windows parser behavior unchanged" spec scenario and the parity-verification tasks to reference the Windows MSVC flow; `openspec validate linux-build-support --strict` passes
