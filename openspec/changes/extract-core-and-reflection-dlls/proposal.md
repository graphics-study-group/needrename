## Why

The engine is currently a single monolithic `engine.dll` with no link-time module boundaries. Seven OBJECT libraries compile into one shared library, allowing bidirectional circular dependencies (Framework↔Asset, Asset↔Render) and a reverse dependency where Core includes Framework headers (`Core/Delegate/ComponentDelegate.h` → `Framework/world/Scene.h`). This prevents incremental builds, blocks independent testing of subsystems, and makes it impossible to reason about the dependency graph at the linker level.

This change executes **Step 1** of the engine modularization roadmap: extract the two zero-dependency leaf modules — **Core** and **Reflection** — into separate shared libraries. This establishes a clean Layer 0 foundation that all other modules can depend on, without introducing any new coupling.

## What Changes

- **Remove Core → Framework reverse dependency**: Move `ComponentDelegate.h` and `EventQueue.h/.cpp` from `engine/Core/` to `engine/Framework/`. These files depend on `Framework/world/Scene.h` and are semantically Framework concepts.
- **Remove dead includes from Core**: Delete unused `#include <MainClass.h>`, `#include <Render/Memory/RenderTargetTexture.h>`, and `#include <vulkan/vulkan.hpp>` from `engine/Core/Functional/SDLWindow.cpp`.
- **Split Reflection.dll from engine.dll**: Extract `EngineLibReflection` OBJECT library into a standalone `Reflection.dll` SHARED library. It has zero engine-module dependencies (only std + glm + nlohmann/json).
- **Split Core.dll from engine.dll**: Extract `EngineLibCore` OBJECT library into a standalone `Core.dll` SHARED library. It links `Reflection.dll` for type registration and glm serialization support. It no longer depends on any Framework or Render headers.
- **Keep other modules in engine.dll**: `EngineLibAsset`, `EngineLibFramework`, `EngineLibPhysics`, `EngineLibRender`, `EngineLibUserInterface` remain OBJECT libraries compiled into `engine.dll`. Their internal dependency graph is not addressed in this change.
- **Update downstream link targets**: All examples, tests, and the editor project must add `Core.dll` and `Reflection.dll` to their link dependencies.
- **Per-module reflection code generation**: Introduce `meta_core` and `meta_reflection` custom targets (or split the existing `meta_engine` target) so each DLL has its own generated type registration code. **BREAKING**: changes the code generation pipeline.

## Capabilities

### New Capabilities
- `core-module`: Standalone Core.dll shared library providing GUID, Transform math, Delegate/Event multicast callbacks, flagbits, SDLWindow wrapper, and TimeSystem. Depends only on Reflection.dll, SDL3, and glm — no other engine modules. Public interface: ~8 header files.
- `reflection-module`: Standalone Reflection.dll shared library providing type registry, JSON serialization archive, compile-time code generation macros, and serialization templates for std types and glm types. Zero engine-module dependencies. Public interface: ~15 header files.

### Modified Capabilities
<!-- None. This change does not alter the behavior of existing capabilities — it only changes the packaging and dependency structure. -->

## Impact

- **engine/CMakeLists.txt**: Major restructure — split `engine` target into `Reflection` (SHARED), `Core` (SHARED), and `engine` (SHARED, remaining 5 OBJECT libs). DLL copy commands for reflection code gen output.
- **engine/Core/**: Remove `Delegate/ComponentDelegate.h`, `Functional/EventQueue.h`, `Functional/EventQueue.cpp`. Clean `Functional/SDLWindow.cpp` dead includes.
- **engine/Framework/**: Receive `component/ComponentDelegate.h`, `world/EventQueue.h`, `world/EventQueue.cpp`. Update all internal references.
- **engine/Reflection/**: Add `dllexport`/`dllimport` macros for public API (Type, Archive, Registrar, serialization functions). No logic changes.
- **engine/__generated__/**: Split `meta_engine` into per-module code gen targets (`meta_core`, `meta_reflection`, and later `meta_engine` for remaining modules).
- **reflection_parser/parser.cmake**: Support per-module `add_reflection_parser()` calls with separate `generated_code_dir` outputs.
- **editor/CMakeLists.txt**: Add `Core` and `Reflection` to link dependencies.
- **example/CMakeLists.txt**: Add `Core` and `Reflection` to link dependencies for all example executables.
- **test/CMakeLists.txt**: Add `Core` and `Reflection` to link dependencies for all test executables.
- **engine/Tests/CMakeLists.txt**: Add `Core` and `Reflection` to link dependencies for unit tests.
- **engine/MainClass.cpp**: Update includes that previously referenced `Core/Functional/EventQueue.h` to use new Framework path.
