# ADR-0009: Extract Reflection as Standalone AnnoRefl Library from Engine

## Status

Proposed

## Motivation

ADR-0008 extracted `Reflection.dll` as a standalone shared library, but it remains inside the
`engine/` directory, using engine-level CMake variables (`REFLECTION_PARSER_DIR`, `EngineDepGlm`,
`EngineDepJson`) and the `Engine::Reflection` / `Engine::Serialization` namespaces. The Python
libclang-based code generation tool (`reflection_parser/`) lives in a separate top-level directory.

This creates three problems:

1. **Artificial engine coupling** â€?The Reflection C++ library has zero engine-module dependencies
   (only glm + json), yet sits inside `engine/` and shares the engine's namespace, making it
   impossible to use standalone without pulling in the entire engine repository.
2. **Naming confusion** â€?`Engine::Reflection` implies it is part of the engine, but reflection and
   serialization are general-purpose utilities. The name "Reflection" also collides with dozens of
   other C++ reflection libraries (`refl-cpp`, `rttr`, `ponder`, `Refureku`, `glaze`, etc.).
3. **Parser-library fragmentation** â€?The Python parser and the C++ runtime library are a coherent
   pair (annotation-driven code generation), yet live in separate top-level directories with no clear
   organizational relationship.

Extracting Reflection into a self-contained `third_party/AnnoRefl/` library resolves all three
problems and prepares the project for future submodule-based external dependency management.

## Decision: Extract Reflection as Standalone AnnoRefl Library

### D-1: Library name `AnnoRefl`

The name `AnnoRefl` is a compound of "Annotation" + "Reflection", describing the library's
distinctive approach: clang `[[clang::annotate]]` attributes drive a Python/libclang parser that
generates type registration and serialization code via Mako templates.

The name was verified to have no conflicts with existing C++ reflection libraries on GitHub or
in package registries.

### D-2: Flat `AnnoRefl::` namespace

The previous `Engine::Reflection` and `Engine::Serialization` namespaces are merged into a single
flat `AnnoRefl::` namespace. This is appropriate for a standalone library: the reflection type
system and serialization are tightly coupled concepts, and separating them only adds verbosity
without meaningful modularity.

```cpp
// Before:
namespace Engine { namespace Reflection { class Type; } }
namespace Engine { namespace Serialization { class Archive; } }

// After:
namespace AnnoRefl { class Type; }
namespace AnnoRefl { class Archive; }
```

### D-3: Location in `third_party/`

The library lives at `third_party/AnnoRefl/`, co-located with genuine third-party dependencies
(glm, json, stb, etc.). However, it is NOT treated as a third-party library in the CMake sense:
it is added via `add_subdirectory` BEFORE `add_compile_options(-w)`, so compiler warnings are
enabled for AnnoRefl source code. The directory is structured to be self-contained, making it
trivial to convert into a git submodule in the future.

### D-4: Parser integrated into the library

The Python reflection parser (`reflection_parser/`) is moved into `third_party/AnnoRefl/parser/`.
The `parser.cmake` file is updated to use `${CMAKE_CURRENT_LIST_DIR}` for self-location instead
of relying on an external `REFLECTION_PARSER_DIR` variable. This means:

```cmake
# Before (parser.cmake):
${REFLECTION_PARSER_DIR}/parser_main.py

# After (parser.cmake â€?self-locating):
${CMAKE_CURRENT_LIST_DIR}/parser_main.py
```

The library's CMakeLists.txt exports `ANROREFL_PARSER_DIR` as a CACHE variable pointing to the
parser directory, so engine/editor/example projects can `include()` the parser CMake module.

### D-5: Parser virtualenv at build time

The Python virtualenv (`parser_env/`) containing libclang and mako is no longer stored in the
source tree. Instead, a CMake variable `ANROREFL_PARSER_ENV_DIR` controls its location:

- Default (not set): `${CMAKE_BINARY_DIR}/AnnoRefl/parser_env/`
- Project override: `${CMAKE_SOURCE_DIR}/build/parser_env/` (shared across debug/release)

The venv is created on first configure and reused on subsequent builds.

### D-6: DLL export macro `ANROREFL_API`

The export header is renamed from `reflection_export.h` to `Export.h`, and the macro from
`REFLECTION_API` to `ANROREFL_API`:

```cpp
// third_party/AnnoRefl/include/AnnoRefl/Export.h
#ifdef ANROREFL_DLL_EXPORTS
    #define ANROREFL_API __declspec(dllexport)
#else
    #define ANROREFL_API __declspec(dllimport)
#endif
```

### D-7: Main header keeps filename

The main header retains its filename `reflection.h` (not renamed to `AnnoRefl.h`) to minimize
diff noise in consumer code. The include path changes from `<AnnoRefl/reflection.h>` to
`<AnnoRefl/reflection.h>`.

### D-8: User macros keep names

User-facing macros (`REFL_SER_CLASS`, `REFL_ENABLE`, `SER_ENABLE`, `REFL_SER_BODY`, etc.) keep
their current names. Only internal namespace references within macro bodies are updated:

```cpp
// Before:
friend class Engine::Reflection::Registrar;
Engine::Serialization::Archive &buffer

// After:
friend class AnnoRefl::Registrar;
AnnoRefl::Archive &buffer
```

### D-9: Dependencies linked directly

AnnoRefl links `glm` and `json` targets directly, rather than going through engine-defined
INTERFACE wrappers (`EngineDepGlm`, `EngineDepJson`). The `GLM_FORCE_DEPTH_ZERO_TO_ONE` definition
is not needed for AnnoRefl's serialization templates.

### D-10: All reflection/serialization tests move with the library

All 22 tests (10 reflection + 12 serialization) are pure unit tests of the reflection/serialization
framework. They define their own test stub types and have zero dependencies on engine types
(Transform, GameObject, Component, Scene, etc.). They move to `third_party/AnnoRefl/tests/`.

### D-11: Extends existing OpenSpec change

This work builds on the `extract-core-and-reflection-dlls` change (ADR-0008) and is recorded as
additional tasks within that same change, rather than a separate change. ADR-0008 extracted
Reflection as a separate DLL within the engine; this ADR completes the journey by moving it
entirely out of the engine repository tree.

## Consequences

- `third_party/AnnoRefl/` contains a self-contained C++ reflection + serialization library with
  an integrated Python code generation parser.
- The old `engine/Reflection/` and `reflection_parser/` directories are deleted.
- All `#include <AnnoRefl/xxx.h>` in engine code become `#include <AnnoRefl/xxx.h>`.
- Generated code templates produce `AnnoRefl::*` namespace references instead of
  `Engine::Reflection::*` / `Engine::Serialization::*`.
- The `meta_core` and `meta_engine` code generation targets continue to work, referencing
  the new namespace.
- Engine DLL no longer links `Reflection.dll`; it links `AnnoRefl.dll` instead.
- Deploy must copy `AnnoRefl.dll` alongside `Core.dll` and `engine.dll`.
- The library is ready to be extracted as a git submodule in a future iteration.
