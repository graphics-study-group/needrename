# Design: Rhi restructure and target naming

## Context

The `extract-gpu-infrastructure-to-rhi` change consolidated all generic GPU infrastructure into `engine/Rhi/` (55 files, unified `Engine::Rhi` namespace). That change is at its final review step and must be archived first. The Rhi module is currently the only engine module without subdirectory organization — every other module already groups by responsibility:

- `engine/Render/`: `Memory/`, `Pipeline/`, `Renderer/`, `RenderSystem/`, `Resource/`
- `engine/Physics/`: `Collision/`, `Solver/`, `gpu_algorithm/`, `shader/`
- `engine/Framework/`: `component/`, `object/`, `world/`
- `engine/Core/`: `Delegate/`, `Functional/`, `Math/`

CMake target naming is inconsistent: standalone DLLs are `Core`, `Rhi`, `engine`, `editor` (no prefix, mixed case) while the OBJECT libraries use `EngineLib*` and dependency interfaces use `EngineDep*`. The include convention is full-path from the engine source root (`#include "Render/Pipeline/RenderGraph/RenderGraph.h"`).

Internal include-dependency analysis of `engine/Rhi/` (per .cpp) shows natural clusters: device/memory types (DeviceInterface, AllocatorState, MemoryAllocation, ...), buffers, textures, pipeline/shader/binding types, submission, and immutable-resource caching.

## Goals / Non-Goals

**Goals:**
- Group `engine/Rhi/` sources into responsibility-based subdirectories without changing any class, namespace, or behavior.
- Unify standalone-DLL CMake target names under the `Engine` prefix, with DLL product names matching.
- Keep the full-path include convention consistent with the rest of the engine.

**Non-Goals:**
- No changes to `EngineLib*` OBJECT libraries, `EngineDep*` interface targets, or `meta_*` reflection targets (deferred; they already carry the family prefix and renaming them is unrelated churn).
- No changes to the `Engine::Rhi` / `Engine` namespaces or any runtime behavior.
- No changes to the `bin/` output layout, the reflection parser, or `projects/` (no CMake there).

## Decisions

### D1. Rhi groups into 6 responsibility subdirectories

```
engine/Rhi/
├── Device/      DeviceInterface, DeviceContext, Structs, DebugUtils, Hasher,
│                AllocatorState, MemoryAllocation, MemoryTypes, MemoryAccessTypes
├── Buffer/      DeviceBuffer, ComputeBuffer, IndexedBuffer, StructuredBuffer, StructuredBufferPlacer
├── Texture/     Texture, ImageTexture, TextureSubresourceView, ImageUtils, ImageUtilsFunc
├── Pipeline/    PipelineEnums(+_reflection), PipelineInfo, ShaderInterface, ShaderParameterLayout,
│                ShaderResourceBinding, ComputeResourceBinding, ComputeStage, ComputeHelpers
├── Submission/  SubmissionHelper
├── Resource/    ImmutableResourceCache
└── (root)       rhi_export.h, RhiReflectionRegistration.cpp, CMakeLists.txt
```

Rationale: memory types (AllocatorState/MemoryAllocation/MemoryTypes/MemoryAccessTypes) are merged into `Device/` for a coarser granularity per user choice; `Submission/` stays a single-file group because SubmissionHelper (432 lines) is still evolving (submission-helper-sync spec) and is cross-cutting (depends on buffers, textures, device); `Resource/` mirrors the existing `Render/Resource/` naming for the immutable-resource cache. The `_reflection.cpp` companion moves with its header.

Alternatives considered: 7 groups with a separate `Memory/` (rejected by user: too fine); a layer-style `Device/Command/Resource` split merging Buffer+Texture (rejected: loses the by-resource-kind discoverability the other modules use).

### D2. CMake targets renamed with `Engine` prefix, DLL names follow

| Now | After | DLL |
|---|---|---|
| `Core` | `EngineCore` | `EngineCore.dll` |
| `Rhi` | `EngineRhi` | `EngineRhi.dll` |
| `engine` | `Engine` | `Engine.dll` |
| `editor` | `EngineEditor` | `EngineEditor.dll` |

Rationale: matches the existing `EngineLib*` / `EngineDep*` prefix family and the `Engine::` namespaces; `EngineCore`/`EngineRhi` are collision-resistant against third-party targets (unlike bare `Core`); DLL names matching target names keeps `$<TARGET_FILE:...>` references obvious. DLL names change with targets so build and deployment are described by one set of identifiers.

Alternatives considered: keep target names and add `Engine::Core`/`Engine::Rhi` ALIAS targets (rejected: does not fix the generic names, only the reference style); keep DLL product names via `OUTPUT_NAME` (rejected by user: dual naming system is worse than one consistent set).

### D3. Includes use full paths into the new groups

All `#include "Rhi/xxx.h"` references become `#include "Rhi/<group>/xxx.h"` (e.g. `#include "Rhi/Device/DeviceInterface.h"`), consistent with how `Render/Pipeline/...` is referenced everywhere. No extra include directories are added, so an un-updated include fails loudly at compile time rather than silently resolving.

### D4. Execution phases with review stops

Phase 1: directory moves + include updates (zero logic change); verify build + ctest; review stop. Phase 2: target/DLL renames across all CMake files; verify build + ctest; review stop. Phase 3: sweep greps (no stale `#include "Rhi/<file>` without group, no stale target names, DLL artifacts in `bin/`), review stop, archive. Phase order means each phase is independently reviewable and revertible.

## Risks / Trade-offs

- Missed include update → compile error on the first file that references it; mitigation: Phase 1 ends with a grep sweep for `#include "Rhi/` leftovers and a full build.
- Missed CMake reference to old target names (e.g. `$<TARGET_FILE:Core>`) → configure/build failure; mitigation: Phase 2 grep sweep for `\b(Core|Rhi|engine|editor)\b` in CMake link contexts; build is the verification.
- Overlap with `extract-gpu-infrastructure-to-rhi` commits → merge noise; mitigation: the prerequisite change is archived before implementation starts.
- `core-module` spec wording becomes stale between archive of the prerequisite and archive of this change → harmless; mitigation: this change's delta spec updates the DLL name at archive time.
- One-time break for downstream consumers of `Core.dll`/`Rhi.dll`/`engine.dll`/`editor.dll` file names → internal engine only, no external consumers; tests/examples are updated in the same change.

## Migration Plan

1. Archive `extract-gpu-infrastructure-to-rhi` (last step 4.5) so `rhi-module` wording lands in `openspec/specs/`.
2. Phase 1 — Rhi directory grouping: move files, update includes repo-wide, build + ctest, grep sweep, review stop.
3. Phase 2 — target renames: `engine/Core/CMakeLists.txt`, `engine/Rhi/CMakeLists.txt`, `engine/CMakeLists.txt`, `editor/CMakeLists.txt`, `example/*/CMakeLists.txt`, `test/CMakeLists.txt`; build + ctest, review stop.
4. Phase 3 — verification sweep and archive.
5. Rollback: revert the phase commit; each phase is a self-contained commit.

## Open Questions

- Whether `EngineLib*` OBJECT libraries should later follow the same rename to `Engine*` (out of scope here; they would only change if a future change splits them into DLLs).
- Whether `meta_*` reflection targets should track the module renames (`meta_enginecore`, ...) — kept unchanged in this change to limit churn; easy follow-up if desired.
