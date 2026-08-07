# Tasks

Phased implementation. Each phase ends with a mandatory review stop: build + tests green, then STOP and hand the diff to the user for review and manual commit. Do NOT proceed to the next phase until the user commits.

## 1. Phase 1 — Pure relocation (files + includes + CMake only, zero logic / namespace change)

- [ ] 1.1 Rename module `GpuContext` → `Rhi`: move `engine/GpuContext/` to `engine/Rhi/`, rename CMake target to `Rhi` (SHARED), update all `#include "GpuContext/xxx.h"` to `"Rhi/xxx.h"` across the repo
- [ ] 1.2 Move buffer types: `DeviceBuffer`, `ComputeBuffer`, `StructuredBuffer`, `StructuredBufferPlacer` from `engine/Render/Memory/` to `engine/Rhi/`
- [ ] 1.3 Move texture types: `Texture`, `ImageTexture`, `TextureSubresourceView` from `engine/Render/Memory/` to `engine/Rhi/`
- [ ] 1.4 Move `ImageUtils` and `MemoryAccessTypes` from `engine/Render/` to `engine/Rhi/`
- [ ] 1.5 Move `ImmutableResourceCache` and `SubmissionHelper` from `engine/Render/RenderSystem/` to `engine/Rhi/`
- [ ] 1.6 Move compute/shader-parameter types: `ComputeStage`, `ComputeResourceBinding`, `ShaderResourceBinding`, `ShaderParameterLayout`, `ShaderInterface` from `engine/Render/` to `engine/Rhi/`
- [ ] 1.7 Move `PipelineEnums` from `engine/Render/Pipeline/` to `engine/Rhi/`
- [ ] 1.8 Update CMake: Render CMakeLists loses moved sources and the `spirv-cross-cpp` PUBLIC link; Rhi CMakeLists gains them (plus `vma`); update all `#include "Render/..."` paths referencing moved files repo-wide
- [ ] 1.9 Phase 1 verification: `cmake --build --preset debug` succeeds and `ctest --preset debug` passes (windowed + headless), with no namespace or logic changes
- [ ] 1.10 PHASE 1 REVIEW STOP: report the diff summary to the user; wait for user review and manual commit before continuing

## 2. Phase 2 — Namespace unification (Engine::Rhi) + asset script

- [ ] 2.1 Rename namespaces of all types now residing in `Rhi`: `Engine::RenderSystemState` → `Engine::Rhi`, `Engine::ImageUtils` → `Engine::Rhi`, `Engine::PipelineUtils` → `Engine::Rhi`, `Engine::ShdrRfl` → `Engine::Rhi`, bare `Engine` (moved types only) → `Engine::Rhi`
- [ ] 2.2 Update all references repo-wide (Render, Physics, Asset, Framework, tests, examples) to the unified namespace; resolve `using` aliases in `Asset/Material/PipelineProperty.h` and other consumers
- [ ] 2.3 Delete the `GpuContext` aggregator class (`engine/Rhi/GpuContext.h/.cpp`)
- [ ] 2.4 Write and run the JSON asset batch script: rewrite namespace-qualified type names in asset files (e.g. `Engine::ImageUtils::ImageFormat` → `Engine::Rhi::ImageFormat`); spot-check affected assets load
- [ ] 2.5 Phase 2 verification: `cmake --build --preset debug` succeeds and `ctest --preset debug` passes
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
