# rhi-restructure-and-target-naming Proposal

## Why

After the `extract-gpu-infrastructure-to-rhi` integration, all 55 sources of the `Rhi` module sit flat in `engine/Rhi/` with no subdirectory grouping, while every other engine module (Render, Physics, Framework, Core) already organizes sources by responsibility. At the same time, the CMake target naming is inconsistent: standalone DLLs (`Core`, `Rhi`, `engine`, `editor`) have no prefix and mixed casing, breaking the `EngineLib*` prefix family, and generic names like `Core` risk colliding with third-party targets. The Rhi file list is now stable right after the extraction, so this is the cheapest moment to restructure and unify naming.

## What Changes

- **BREAKING** Restructure `engine/Rhi/` into responsibility-based subdirectories: `Device/` (device, context, allocator, memory), `Buffer/`, `Texture/`, `Pipeline/`, `Submission/`, `Resource/`. Update every `#include "Rhi/..."` repo-wide to `#include "Rhi/<group>/..."` following the project's full-path include convention. Namespaces, class names, and behavior are unchanged.
- **BREAKING** Rename CMake targets and their DLL products: `Core` → `EngineCore` (`EngineCore.dll`), `Rhi` → `EngineRhi` (`EngineRhi.dll`), `engine` → `Engine` (`Engine.dll`), `editor` → `EngineEditor` (`EngineEditor.dll`).
- The `Rhi`/`Core` CMakeLists use `file(GLOB_RECURSE ...)`, so subdirectory moves require no source-list changes.
- **Unchanged**: `EngineLib*` OBJECT libraries, `EngineDep*` interface targets, `meta_*` reflection targets, the `Engine::Rhi` namespace, the unified `bin/` output layout, and all runtime behavior.

## Capabilities

### New Capabilities

- `rhi-directory-structure`: The `Rhi` module SHALL organize its sources into responsibility-based subdirectories (`Device/`, `Buffer/`, `Texture/`, `Pipeline/`, `Submission/`, `Resource/`) with full-path includes.
- `module-target-naming`: The engine SHALL name its shared-library CMake targets with the `Engine` prefix (`EngineCore`, `EngineRhi`, `Engine`, `EngineEditor`) and produce DLLs of the same names.

### Modified Capabilities

- `core-module`: The Core module's DLL product name changes from `Core.dll` to `EngineCore.dll` (requirement wording update only; build/link behavior unchanged).

## Impact

- **Code**: `engine/Rhi/*` file moves; include-path updates across `engine/` (Rhi self-includes, Render, Physics, Asset, Framework), `editor/`, `example/`, `test/` (~100+ references).
- **Build**: `engine/Core/CMakeLists.txt`, `engine/Rhi/CMakeLists.txt`, `engine/CMakeLists.txt` (links, `$<TARGET_FILE:Core>` copy command, dependencies), `editor/CMakeLists.txt`, `example/*/CMakeLists.txt`, `test/CMakeLists.txt` (~15+ link references).
- **Runtime**: DLL file names in `bin/` change (`EngineCore.dll`, `EngineRhi.dll`, `Engine.dll`, `EngineEditor.dll`); same-directory loading on Windows means no loader changes.
- **Prerequisite**: `extract-gpu-infrastructure-to-rhi` must be archived before implementation starts (its `rhi-module` spec lands `Rhi.dll` wording that this change then renames to `EngineRhi.dll`).
- **Verification**: `cmake --build --preset debug` and `ctest --preset debug` green (48/48 baseline), no behavior change.
