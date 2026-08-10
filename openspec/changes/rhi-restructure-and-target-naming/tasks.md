# Tasks: rhi-restructure-and-target-naming

Prerequisite: `extract-gpu-infrastructure-to-rhi` is archived (its 4.5 review/commit/archive is done) before starting Phase 1.

## 1. Phase 1: Rhi directory grouping

- [x] 1.1 Move `engine/Rhi/` sources into the six subdirectories: `Device/` (DeviceInterface.*, DeviceContext.*, Structs.h, DebugUtils.h, Hasher.hpp, AllocatorState.*, MemoryAllocation.*, MemoryTypes.h, MemoryAccessTypes.h), `Buffer/` (DeviceBuffer.*, ComputeBuffer.*, IndexedBuffer.*, StructuredBuffer.*, StructuredBufferPlacer.*), `Texture/` (Texture.*, ImageTexture.*, TextureSubresourceView.*, ImageUtils.*, ImageUtilsFunc.h), `Pipeline/` (PipelineEnums.h, PipelineEnums_reflection.cpp, PipelineInfo.*, ShaderInterface.h, ShaderParameterLayout.*, ShaderResourceBinding.*, ComputeResourceBinding.*, ComputeStage.*, ComputeHelpers.h), `Submission/` (SubmissionHelper.*), `Resource/` (ImmutableResourceCache.*); `rhi_export.h` and `RhiReflectionRegistration.cpp` stay at the root
- [x] 1.2 Update includes inside `engine/Rhi/` (self-references between moved files) to the new group paths
- [x] 1.3 Update includes repo-wide outside the module: `engine/` (Render, Physics, Asset, Framework, MainClass), `editor/`, `example/`, `test/` (all `#include "Rhi/xxx.h"` → `#include "Rhi/<group>/xxx.h"`)
- [x] 1.4 Phase 1 verification: `cmake --build --preset debug` succeeds and `ctest --preset debug` passes (same count as baseline 48/48); grep confirms zero group-less `#include "Rhi/` references remain
- [x] 1.5 PHASE 1 REVIEW STOP: report the diff summary to the user; wait for user review and manual commit before continuing

## 2. Phase 2: Target and DLL renames

- [x] 2.1 `engine/Core/CMakeLists.txt`: rename `add_library(Core ...)` → `EngineCore`, update `target_compile_definitions`/`get_include_directories_for_target(Core ...)` self-references; `meta_core` target name unchanged
- [x] 2.2 `engine/Rhi/CMakeLists.txt`: rename `add_library(Rhi ...)` → `EngineRhi`, update all self-references (`get_include_directories_for_target`, `add_dependencies(Rhi ...)`); `meta_rhi` target name unchanged
- [x] 2.3 `engine/CMakeLists.txt`: rename `add_library(engine ...)` → `Engine`; update `target_link_libraries` (PUBLIC/PRIVATE, `Core`/`Rhi` → `EngineCore`/`EngineRhi`), the POST_BUILD copy command `$<TARGET_FILE:Core>` → `$<TARGET_FILE:EngineCore>`, `add_dependencies(engine ...)` → `Engine`, and reflection parser `get_include_directories_for_target(engine ...)` → `Engine`; `meta_engine` target name unchanged
- [x] 2.4 `editor/CMakeLists.txt`: rename `add_library(editor ...)` → `EngineEditor`; link `engine` → `Engine`
- [x] 2.5 `example/*/CMakeLists.txt`: update all `target_link_libraries(... engine)` → `Engine` and `editor` → `EngineEditor` (editor_run_game_example links both)
- [x] 2.6 `test/CMakeLists.txt`: update all `target_link_libraries(... engine)` → `Engine` (all `*_test` executables)
- [x] 2.7 Phase 2 verification: `cmake --build --preset debug` succeeds and `ctest --preset debug` passes; build output directory contains `EngineCore.dll`, `EngineRhi.dll`, `Engine.dll`, `EngineEditor.dll` and no `Core.dll`/`Rhi.dll`/`engine.dll`/`editor.dll`
- [x] 2.8 PHASE 2 REVIEW STOP: report the diff summary to the user; wait for user review and manual commit before continuing

## 3. Phase 3: Final sweep and archive

- [ ] 3.1 Sweep greps: no stale target names in CMake link contexts (`Core`/`Rhi`/`engine`/`editor` as linked targets), no group-less `#include "Rhi/` references, no remaining `GpuContext` references
- [ ] 3.2 Final verification: clean `cmake --build --preset debug` and `ctest --preset debug` all green; runtime smoke check of one example (e.g. `physics_example` or a test executable) to confirm DLL loading with the new names
- [ ] 3.3 PHASE 3 REVIEW STOP: report the full diff summary to the user; wait for user review and manual commit, then archive the change
