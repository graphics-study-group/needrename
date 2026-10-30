# SDK consumer example

A standalone CMake project that builds against the **installed** engine SDK
only — no engine source paths, no in-tree targets. It answers one question: *is
the installed prefix sufficient for an outside project?*

It is **not** part of the engine build: [`example/CMakeLists.txt`](../CMakeLists.txt)
does not add it, so a normal build never configures or compiles it. Build it by
hand into its own `build/` directory when you want to check an installation.

## What the two programs check

| Target | Proves |
| --- | --- |
| `sdk_consumer_minimal` | `find_package(needrename)` and `needrename::Engine` link and run; the runtime data locator resolves `share/needrename` from the executable's own location, with no environment variable; `builtin_assets/`, `projects/` and `assets/` exist; prints the installed shader form (`baked SPIR-V` for a Release install, `GLSL` for a Debug one) |
| `sdk_consumer_reflection` | `needrename_add_reflection()` runs the shipped parser and `.pkl` type lists over a consumer header; the component derives from `Engine::Transform` and round-trips through the archive format |

## Defaults

The project looks for the installation at `install/` of the checkout that
contains it, and for the reflection parser's interpreter in the checkout's
`.venv/`, so the commands below need no paths. Consume a prefix elsewhere with
`-DCMAKE_PREFIX_PATH=<prefix>`, and a different interpreter with
`-DPython3_EXECUTABLE=<python>`.

Requirements: SDL3 with a CMake config package, plus the Vulkan SDK
(`VULKAN_SDK`) on the consumer machine — the same setup as building the engine.

The configuration of the prefix and of the build must match: a Release install
ships `needrenameTargets-release.cmake`, a Debug install ships
`needrenameTargets-debug.cmake`.

## Windows

From the checkout root:

```powershell
cmake -S example\sdk_consumer -B example\sdk_consumer\build -G "Visual Studio 18 2026"
cmake --build example\sdk_consumer\build --config Release
```

The engine's DLLs resolve from `PATH`; the data root resolves from the
executable's location, so `sdk_consumer_minimal` is copied into the prefix's
`bin/` for its run and removed afterwards:

```powershell
$env:PATH = "$PWD\install\bin;$env:PATH"
Remove-Item Env:\NEEDRENAME_DATA_ROOT -ErrorAction SilentlyContinue

Copy-Item .\example\sdk_consumer\build\Release\sdk_consumer_minimal.exe .\install\bin\
.\install\bin\sdk_consumer_minimal.exe
Remove-Item .\install\bin\sdk_consumer_minimal.exe

.\example\sdk_consumer\build\Release\sdk_consumer_reflection.exe
```

## Linux

```sh
source ~/VulkanSDK/current/setup-env.sh     # if the SDK is not already in the environment

cmake -S example/sdk_consumer -B example/sdk_consumer/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build example/sdk_consumer/build

prefix="$PWD/install"
export PATH="$prefix/bin:$PATH"
unset NEEDRENAME_DATA_ROOT
cp example/sdk_consumer/build/sdk_consumer_minimal "$prefix/bin/"
( cd /tmp && "$prefix/bin/sdk_consumer_minimal" )
rm "$prefix/bin/sdk_consumer_minimal"

example/sdk_consumer/build/sdk_consumer_reflection
```

Use `-DCMAKE_BUILD_TYPE=Debug` when the prefix was installed from a Debug build.

The checkout's `.venv` holds a Windows interpreter, so on Linux point
`-DPython3_EXECUTABLE` at one that has the `clang` and `mako` packages.

## Expected output

```text
data root          : <prefix>/share/needrename
resolved through   : executable-relative share/needrename
  builtin_assets : <prefix>/share/needrename/builtin_assets
  projects : <prefix>/share/needrename/projects
  assets : <prefix>/share/needrename/assets
  test_data : <prefix>/share/needrename/test_data
shader archives    : 12 (SPIR-V: 12, GLSL sources: 0)
shader storage form: baked SPIR-V

engine base type   : Engine::Transform registered
round trip         : speed=42.5 samples=3 z=3.0 -> PASSED
```

`test_data` prints `(not installed)` for a default install; that is not a
failure. Exit code 0 and `PASSED` mean the installation is complete and
self-sufficient.

Reflection generation writes its per-header wrapper to `src/__generated__/`, next
to the header, as it does for the in-tree examples; that directory is
git-ignored.
