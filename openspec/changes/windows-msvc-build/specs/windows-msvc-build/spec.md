## Purpose

Makes Windows a first-class MSVC build platform for the engine: Visual Studio generator presets, a reflection parser driven by the VS-bundled libclang with MSVC-target arguments, and complete removal of the MSYS2 build flow, all documented for setup on a fresh machine.

## ADDED Requirements

### Requirement: Windows builds with the MSVC toolchain
The project SHALL configure, build, and test successfully on Windows with the MSVC toolchain (Visual Studio 2022 or later, verified on VS2026) via the Visual Studio multi-config generator, with the build tree under `build/msvc`.

#### Scenario: Configure with the msvc preset
- **WHEN** `cmake --preset msvc` runs on a machine with Visual Studio, the Vulkan SDK, and `SDL3_ROOT` set
- **THEN** configuration SHALL succeed with no platform-specific errors
- **AND** the Vulkan and SDL3 dependencies SHALL be discovered without extra cache variables

#### Scenario: Build all targets on Windows MSVC
- **WHEN** `cmake --build --preset msvc-debug` runs
- **THEN** all engine DLLs, apps, examples, and test executables SHALL build successfully with cl.exe

#### Scenario: Tests run on Windows MSVC
- **WHEN** `ctest --preset msvc-debug` runs
- **THEN** the test suite SHALL pass, including the reflection/serialization tests that validate the generated reflection code

### Requirement: MSYS2 build flow is removed
The repository SHALL no longer reference or support the MSYS2 CLANG64 Windows build flow.

#### Scenario: No MSYS2 documentation remains
- **WHEN** a developer searches the build instructions
- **THEN** no document SHALL instruct installing or configuring MSYS2 packages for Windows builds
- **AND** the Windows build instructions SHALL describe the MSVC flow instead

#### Scenario: No MSYS2-specific parser logic remains
- **WHEN** the reflection parser CMake logic is inspected
- **THEN** no branch SHALL depend on MSYS2 install prefixes, the `x86_64-w64-windows-gnu` target, `libstdc++`, or an MSYS2 clang resource directory

### Requirement: Reflection parser uses the VS-bundled libclang on Windows
On Windows, the reflection parser SHALL load libclang from the Visual Studio "C++ Clang tools for Windows" component (`VC\Tools\Llvm\x64\bin\libclang.dll`) via `LIBCLANG_LIBRARY_PATH`, and SHALL NOT rely on pip's bundled libclang DLL.

#### Scenario: libclang DLL located from the VS installation
- **WHEN** CMake configures with the MSVC toolchain and the C++ Clang tools component installed
- **THEN** the parser invocation SHALL run with `LIBCLANG_LIBRARY_PATH` pointing at the VS Llvm bin directory

#### Scenario: Clang tools component missing
- **WHEN** CMake configures with the MSVC toolchain and no `VC\Tools\Llvm\x64\bin\libclang.dll` exists
- **THEN** configuration SHALL fail with a clear error message naming the missing component and how to install it

### Requirement: Reflection parser parses MSVC-target code
The libclang parse arguments on Windows SHALL target the MSVC ABI and match the MSVC standard library in use, so the generated reflection code compiles and the reflection/serialization tests pass.

#### Scenario: MSVC-target parse arguments
- **WHEN** the parser runs on Windows MSVC
- **THEN** the parse arguments SHALL include the `x86_64-pc-windows-msvc` target and MSVC compatibility flags
- **AND** an `-fmsc-version` matching the active MSVC version SHALL be supplied
- **AND** the MSVC standard library and Windows SDK include directories SHALL be visible to the parser

#### Scenario: Missing headers do not abort parsing
- **WHEN** a parsed header references an include that cannot be resolved
- **THEN** parsing SHALL still succeed and generate reflection code for the annotated declarations (dependency-generation flags tolerate missing headers)

### Requirement: Windows MSVC build instructions are documented
The repository SHALL contain Windows build documentation covering every external dependency a developer must install manually and the runtime environment requirements.

#### Scenario: Dependency table exists
- **WHEN** a developer reads the Windows MSVC build instructions
- **THEN** the instructions SHALL list the required Visual Studio version and components (including C++ Clang tools for Windows), the Vulkan SDK, SDL3 provisioning via the official VC dev package with the `SDL3_ROOT` environment variable, and the Python `.venv` setup with the parser requirements
- **AND** the instructions SHALL state the SDL3 environment variable name exactly (`SDL3_ROOT`)

#### Scenario: Runtime environment documented
- **WHEN** a developer runs a Debug build with Vulkan validation layers
- **THEN** the instructions SHALL state where validation layers come from and that the engine skips them gracefully when unavailable
