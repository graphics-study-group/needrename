## Purpose

Makes Linux a first-class build and run platform for the engine and establishes cross-platform rules for the reflection parser's Python environment, shared-library loading, and build documentation.

## ADDED Requirements

### Requirement: Linux is a supported build platform
The engine SHALL configure, build, and test successfully on Linux (Ubuntu 24.04 WSL2 verified) with the clang/ninja toolchain, in addition to the existing Windows MSYS2 CLANG64 flow.

#### Scenario: Configure on Linux
- **WHEN** CMake is configured on Linux with the clang/ninja toolchain
- **THEN** configuration SHALL succeed with no platform-specific errors
- **AND** the Vulkan and SDL3 dependencies SHALL be discovered successfully

#### Scenario: Build all targets on Linux
- **WHEN** `cmake --build` runs on Linux
- **THEN** all engine DLLs (shared libraries), apps, examples, and test executables SHALL build successfully

#### Scenario: Tests run on Linux
- **WHEN** `ctest` runs on Linux
- **THEN** headless tests SHALL pass on a machine without a display
- **AND** windowed tests SHALL run when a display (WSLg or X11) is available

### Requirement: Reflection parser uses a user-provided Python interpreter
The reflection parser SHALL NOT create or manage its own Python virtual environment. It SHALL use a Python interpreter supplied by the user through the standard CMake Python3 discovery (e.g. `Python3_EXECUTABLE` preset cache variable pointing at a user-created `.venv`).

#### Scenario: User provides a working interpreter
- **WHEN** the user configures with `Python3_EXECUTABLE` pointing at an environment that has the parser requirements installed
- **THEN** configuration SHALL succeed without creating any venv
- **AND** the reflection code generation targets SHALL run with that interpreter

#### Scenario: Parser requirements missing
- **WHEN** the user configures with an interpreter that lacks the parser requirements
- **THEN** configuration SHALL fail with a clear, actionable error message
- **AND** the error message SHALL state exactly which imports are missing and how to install them

#### Scenario: No Windows-specific venv layout assumed
- **WHEN** the parser environment is checked or used on any platform
- **THEN** no Windows-only path (e.g. `Lib/site-packages`) SHALL be required for readiness detection
- **AND** the parser SHALL NOT require the virtual environment directory to be named `parser_env`

### Requirement: Reflection parser produces equivalent output across platforms
The libclang parsing arguments SHALL be chosen per platform so that the generated reflection code is equivalent to the Windows MSYS2 output, and the same configuration change MUST NOT alter Windows parser behavior.

#### Scenario: Generated code parity
- **WHEN** a reflection generation target (e.g. `meta_core`) runs on Linux with the same engine headers
- **THEN** the generated reflection code SHALL be equivalent to the code generated on Windows for the same headers (verified by diff)

#### Scenario: Windows parser behavior unchanged
- **WHEN** the parser runs on Windows MSYS2 CLANG64
- **THEN** the libclang arguments SHALL remain exactly as before this change (target triple, resource dir, C++ include paths, `LIBCLANG_LIBRARY_PATH`)

### Requirement: Linux executables resolve sibling shared libraries
On Linux, executables in the unified output directory SHALL load the engine's shared libraries located in the same directory without extra environment setup.

#### Scenario: Run from the build bin directory
- **WHEN** a test executable in `build/<config>/bin/` is launched directly on Linux
- **THEN** it SHALL find and load the engine `.so` libraries in the same directory via `$ORIGIN`

### Requirement: Linux build instructions are documented
The repository SHALL contain platform-specific build documentation covering dependency installation and runtime requirements for Linux.

#### Scenario: Linux dependency table exists
- **WHEN** a developer reads the Linux build instructions
- **THEN** the instructions SHALL list every required dependency and its install command for the verified distribution (Ubuntu 24.04)
- **AND** the instructions SHALL cover SDL3 provisioning (source build on Ubuntu 24.04)

#### Scenario: WSL2 graphics guidance exists
- **WHEN** a developer runs windowed/rendering tests in WSL2
- **THEN** the instructions SHALL explain the available Vulkan ICDs (gfxstream GPU passthrough, lavapipe software) and display requirements
