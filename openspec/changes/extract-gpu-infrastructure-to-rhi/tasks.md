# Tasks

Phased implementation. Each phase ends with a mandatory review stop: build + tests green, then STOP and hand the diff to the user for review and manual commit. Do NOT proceed to the next phase until the user commits.

## 1. Phase 1 — Pure relocation (files + includes + CMake only, zero logic / namespace change)

> Phase 1 adjustments (user decisions during implementation):
> - Rhi is an OBJECT library merged into `engine.dll` for now; the standalone `Rhi.dll` split moves to Phase 4. Constructor signatures keep `RenderSystem&`; call-site adaptation stays in Phase 3.
> - `GPU_CONTEXT_API` expands empty during the OBJECT merge (a dllimport class member would be stripped from the export table; MinGW's default `--export-all-symbols` handles consumers).
> - The dispatcher storage in `DeviceInterface.cpp` is temporarily removed (MainClass.cpp provides it while merged); restore in Phase 4.
> - Dependency-closure additions to the moved set: `Hasher.hpp`, `ImageUtilsFunc.h`, `PipelineInfo.h`, `ToVkCompareOp` (into `PipelineEnums.h`). `IndexedBuffer` stays in Render for Phase 1 (link order is fine while merged); moves in Phase 3 with the `ComputeResourceBinding` rework.

- [x] 1.1 Rename module `GpuContext` → `Rhi`: move `engine/GpuContext/` to `engine/Rhi/`, rename CMake target to `Rhi` (SHARED → OBJECT for Phase 1), update all `#include "GpuContext/xxx.h"` to `"Rhi/xxx.h"` across the repo
- [x] 1.2 Move buffer types: `DeviceBuffer`, `ComputeBuffer`, `StructuredBuffer`, `StructuredBufferPlacer` from `engine/Render/Memory/` to `engine/Rhi/`
- [x] 1.3 Move texture types: `Texture`, `ImageTexture`, `TextureSubresourceView` from `engine/Render/Memory/` to `engine/Rhi/`
- [x] 1.4 Move `ImageUtils`, `ImageUtilsFunc.h`, `Hasher.hpp`, and `MemoryAccessTypes` from `engine/Render/` to `engine/Rhi/`
- [x] 1.5 Move `ImmutableResourceCache` and `SubmissionHelper` from `engine/Render/RenderSystem/` to `engine/Rhi/`
- [x] 1.6 Move compute/shader-parameter types: `ComputeStage`, `ComputeResourceBinding`, `ShaderResourceBinding`, `ShaderParameterLayout`, `ShaderInterface`, `PipelineInfo.h` from `engine/Render/` to `engine/Rhi/`
- [x] 1.7 Move `PipelineEnums` from `engine/Render/Pipeline/` to `engine/Rhi/` (with `ToVkCompareOp` moved in from `PipelineUtils.hpp`)
- [x] 1.8 Update CMake: Render CMakeLists loses moved sources and the `spirv-cross-cpp` PUBLIC link; `EngineLibRhi` (OBJECT) gains them (plus `vma`); update all `#include "Render/..."` paths referencing moved files repo-wide (engine, test, example, editor)
- [x] 1.9 Phase 1 verification: `cmake --build --preset debug` succeeds and `ctest --preset debug` passes (48/48), with no namespace or logic changes
- [ ] 1.10 PHASE 1 REVIEW STOP: report the diff summary to the user; wait for user review and manual commit before continuing

## 2. Phase 2 — Namespace unification (Engine::Rhi) + asset script

> Phase 2 notes (implementation):
> - Namespace block forms: `namespace Engine { namespace RenderSystemState { ... } }` (indented) and bare-`Engine` blocks in Rhi files were converted to `namespace Engine::Rhi`; the `ImageUtils`/`PipelineUtils`/`ShdrRfl` nesting layers were removed (types lifted into `Engine::Rhi`).
> - `MemoryTypes` nesting layer inside `Rhi` was kept, with `using` aliases (`BufferType`, `ImageMemoryType`, ...) exposed at `Engine::Rhi` scope.
> - `PipelineUtils.hpp` functions (`ToVkBlendFactor`, `ToVulkan*`, `pipeline_runtime_info_hasher`, ...) remain in `Engine::PipelineUtils`; only the enums and `ToVkCompareOp` moved to `Engine::Rhi`.
> - `class X;` forward declarations of Rhi types now use `namespace Rhi { class X; }` blocks (qualified forward decls unsupported by libclang); qualified `class Engine::RenderSystem;` is written as a top-level `namespace Engine { class RenderSystem; }` block before the Rhi block.
> - Asset scan (49 `.asset` files under `builtin_assets/`): no changes required. `%type` fields reference only Asset-module types (`Engine::MaterialAsset`, `Engine::PipelineProperties::*`, ...) which did not move; enum values are serialized by value name (`"Texture"`, `"Less"`, `"None"`, `"R11G11B10UFloat"`) and those names did not change; there are zero `Engine::ImageUtils::` / `Engine::PipelineUtils::` / `Engine::ShdrRfl::` / `Engine::RenderSystemState::` references in any asset.
> - Both GpuContext-class tests (`gpu_context_standalone_test`, `submission_helper_test`) now construct `Rhi::DeviceInterface` + `Rhi::AllocatorState` directly.

- [x] 2.1 Rename namespaces of all types now residing in `Rhi`: `Engine::RenderSystemState` → `Engine::Rhi`, `Engine::ImageUtils` → `Engine::Rhi`, `Engine::PipelineUtils` → `Engine::Rhi`, `Engine::ShdrRfl` → `Engine::Rhi`, bare `Engine` (moved types only) → `Engine::Rhi`
- [x] 2.2 Update all references repo-wide (Render, Physics, Asset, Framework, tests, examples) to the unified namespace; resolve `using` aliases in `Asset/Material/PipelineProperty.h` and other consumers
- [x] 2.3 Delete the `GpuContext` aggregator class (`engine/Rhi/GpuContext.h/.cpp`) and rework its two test consumers to construct the facilities directly
- [x] 2.4 Asset scan: all 49 `.asset` files checked — `%type` only references Asset-module types and enum values serialize by name; zero affected references, batch script not required
- [x] 2.5 Phase 2 verification: `cmake --build --preset debug` succeeds and `ctest --preset debug` passes (48/48)
- [ ] 2.6 PHASE 2 REVIEW STOP: report the diff summary to the user; wait for user review and manual commit before continuing

## 3. Phase 3 — Physics interface rework (behavior point)

- [ ] 3.1 Change `ISolver::GPUStep` and `PhysicsSystem::GPUStep` to raw `vk::CommandBuffer`; adapt `XpbdGpuSolver`, `DummySolver`, `SpatialHashBroadDetector`, `ConvexCollisionDetector`, `RadixSort`, `ParallelScan`, `CompactUnique` `GPUStep`/`Record` signatures
- [ ] 3.2 Add Rhi compute helpers as free functions over `vk::CommandBuffer` + `ComputeStage`/`ComputeResourceBinding` (bind stage, bind resource, dispatch); physics call sites switch from the Render `CommandBuffer` wrapper to these helpers
- [ ] 3.3 Change constructor signatures: `XpbdGpuSolver`, `DummySolver`, detectors, and algorithms from `RenderSystem&` to `(const Rhi::DeviceInterface&, const Rhi::AllocatorState&)`; `ComputeResourceBinding` to `(const Rhi::DeviceInterface&, ComputeStage&)`; `PhysicsScene::RefreshGpuBuffers`/`SyncGpuBuffers` take `(const Rhi::AllocatorState&, Rhi::SubmissionHelper&)`
- [ ] 3.4 Adapt call sites: `MainClass::LoadProject` solver construction, all `test/*.cpp` and example solver/detector constructions
- [ ] 3.5 Remove physics→render bridge: delete `SetModelMatricesBuffer` calls in `XPBDGpuSolver::GPUStep` and `PhysicsScene::SyncGpuBuffers`; add the forward in `MainClass::RunOneFrame` after physics flush/step using `GetGpuBuffers().model_matrices` (skip when physics scene is null)
- [ ] 3.6 Verify physics module no longer includes any `Render/` header (grep check)
- [ ] 3.7 Phase 3 verification: `cmake --build --preset debug` succeeds and `ctest --preset debug` passes (windowed + headless); physics behavior unchanged
- [ ] 3.8 PHASE 3 REVIEW STOP: report the diff summary to the user; wait for user review and manual commit before continuing

## 4. Phase 4 — Cleanup (reflection, exports, tests)

- [ ] 4.1 Add Rhi reflection registration: move generated `__generated__` outputs (e.g. `ImageUtils.h.inc`) to the Rhi module, add `reflection_init.inc` + `RegisterRhiTypes()` per the per-DLL registration pattern, call it from `MainClass::Initialize`; extend the reflection parser scan list with `engine/Rhi/`
- [ ] 4.2 Rename export macro: `gpu_context_export.h` → `rhi_export.h` with `RHI_API` / `RHI_DLL_EXPORTS`; apply to moved DLL-boundary types
- [ ] 4.3 Sweep tests and examples for stale namespace/include references (no `RenderSystemState` or old paths for moved types)
- [ ] 4.4 Final verification: `cmake --build --preset debug` and `ctest --preset debug` all green; grep confirms `Rhi` has zero `engine/Render/` includes; `Asset → Rhi` dependency in effect
- [ ] 4.5 PHASE 4 REVIEW STOP: report the full diff summary to the user; wait for user review and manual commit, then archive the change
