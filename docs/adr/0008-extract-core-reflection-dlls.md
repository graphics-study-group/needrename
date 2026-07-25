# ADR-0008: Extract Core and Reflection as Separate Shared Libraries

## Status

Proposed

## Motivation

The engine compiles 7 OBJECT libraries into a single `engine.dll` shared library. This creates three
problems:

1. **No link-time module boundaries** — All headers are globally visible through
   `EngineLibHeaderInterface`. A developer can include any module's header from any other module,
   and the compiler won't catch illegal dependencies until link time (if at all, for header-only
   templates).
2. **Core has a reverse dependency on Framework** — `Core/Delegate/ComponentDelegate.h` and
   `Core/Functional/EventQueue.h` include `<Framework/world/Scene.h>`. Core cannot serve as a true
   leaf dependency.
3. **Reflection has no engine dependencies but is fused into the monolith** — The reflection
   module (`EngineLibReflection`) depends only on std, glm, and nlohmann/json. It is identical in
   character to third-party libraries and yet is compiled into `engine.dll`, making it impossible
   to reuse in tooling or test contexts without linking the entire engine.

Extracting Core and Reflection as standalone DLLs is Step 1 of the engine modularization roadmap.
It establishes a clean Layer 0/1 foundation that all other modules can depend on unidirectionally.

## Decision: Extract Reflection.dll and Core.dll from engine.dll

### D-1: Two DLLs, not one

**Reflection.dll** (Layer 0, zero engine dependencies) and **Core.dll** (Layer 1, depends on
Reflection.dll). Rejected a single `Foundation.dll` because Reflection has a distinct responsibility
(type system + serialization) from Core (math + callbacks + platform), and the json dependency
should not be forced on Core-only consumers.

### D-2: Per-DLL type registration init flow

The generated type registration code (`reflection_init.inc`) historically lived in
`reflection.cpp` (Reflection module) and called ALL type registrars, including
`Register_Engine6Transform9()` whose definition lives in Core.dll. After the split, this would
create a circular DLL dependency (Reflection.dll → Core.dll while Core.dll → Reflection.dll).

**Solution**: Each DLL provides its own registration entry point. The generated `RegisterAllTypes()`
function uses `static` linkage, so each DLL has its own copy via its `meta_*` target:

```cpp
// Reflection.dll:
void Initialize() { Registrar::RegisterBasicTypes(); }  // no longer calls RegisterAllTypes()

// Core.dll (CoreReflectionRegistration.cpp):
CORE_API void RegisterCoreTypes();  // wraps meta_core/reflection_init.inc → calls RegisterAllTypes()

// engine.dll (MainClass.cpp directly includes meta_engine/reflection_init.inc):
// RegisterAllTypes() — static, no collision with Core's copy

// MainClass::Initialize() call order:
Reflection::Initialize();            // 1. basic types (int, float, glm::vec3...)
RegisterCoreTypes();                 // 2. Transform
RegisterAllTypes();                  // 3. Asset, Framework, Physics, etc. (from meta_engine)
```

The `reflection_init.inc` is split into per-module versions emitted by `meta_core` and
`meta_engine` cmake targets.

### D-3: DLL import/export macros

Two separate macros: `REFLECTION_API` and `CORE_API`, each controlled by a compile definition
(`REFLECTION_DLL_EXPORTS` / `CORE_DLL_EXPORTS`) set on the respective SHARED library target.

Rejected a single `ENGINE_API` macro because Reflection is imported by Core, Core is imported by
engine, and each needs independent import/export control.

### D-4: Granular external dependency INTERFACE targets

The monolithic `EngineLibExternalDependency` is split into:
- `EngineDepGlm` (glm + GLM_FORCE_DEPTH_ZERO_TO_ONE)
- `EngineDepJson` (nlohmann/json)
- `EngineDepSdl` (SDL3)
- `EngineDepVulkan` (Vulkan + vma + glslang + VULKAN_HPP_*)
- `EngineDepImgui` (imgui)

Reflection links only EngineDepGlm + EngineDepJson. Core links only EngineDepGlm +
EngineDepSdl. This prevents Vulkan and imgui from leaking into the bottom layers.

### D-5: Per-module code generation targets

`meta_core` (Core types only: Transform) generates to `engine/Core/__generated__/meta_core/`,
defined in `engine/Core/CMakeLists.txt`. `meta_engine` (remaining types: Asset, Framework,
Physics, Render, UI) generates to `engine/__generated__/meta_engine/`, defined in
`engine/CMakeLists.txt` with `parent_projects meta_core_generation` for cross-DLL base type
resolution. No `meta_reflection` target needed — Reflection has zero `REFL_SER_CLASS`
annotated types.

### D-6: File migrations

- `Core/Delegate/ComponentDelegate.h` → `Framework/component/`
- `Core/Functional/EventQueue.h/.cpp` → `Framework/world/`
- Remove `#include <Reflection/serialization_glm.h>` from `Transform.h` (only needed in .cpp)
- Remove dead includes from `SDLWindow.cpp` (MainClass.h, RenderTargetTexture.h, vulkan.hpp)

### D-7: Include path policy (temporary)

Keep the global include path (`${ENGINE_SOURCE_DIR}`) for all DLLs. Per-DLL `PUBLIC_HEADER`
directories to enforce compile-time module boundaries are deferred to a future improvement.

## Consequences

- `Core.dll` can be linked by tools, tests, and external projects without pulling in Vulkan,
  imgui, or the engine monolith.
- `Reflection.dll` can be used standalone for code generation tooling or serialization-only
  workloads.
- All 7 existing OBJECT library targets are removed; replaced by 2 SHARED (`Reflection`,
  `Core`) + 5 remaining OBJECT libraries compiled into a smaller `engine.dll`.
- The reflection code generation pipeline supports per-module `meta_*` targets, enabling
  each future DLL split (Asset, Physics, Render, etc.) to have its own type registration.
- Build time increases slightly due to extra reflection parser pass (`meta_core`), mitigated
  by incremental `task_stamped` files.
- Deployment must copy 2 additional DLLs (`Core.dll`, `Reflection.dll`) to the output
  directory alongside `engine.dll`.
- Remaining modules (Asset, Framework, Physics, Render, UI) stay in `engine.dll` as OBJECT
  libraries. Their internal circular dependencies are addressed in future ADRs.
