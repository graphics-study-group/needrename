# Design: Physics push constants

## Context

`decouple-rhi-from-frame-rotation` parameterized `ComputeResourceBinding` rotation depth (`slot_count`, default 1) and adapted physics call sites with literal `3` as a transitional state, pre-planning this follow-up. Current state of physics GPU code:

- 7 components allocate bindings with `slot_count = 3` (37 call sites) and maintain `m_frame_counter++ % 3` — render-frame semantics with no meaning for physics, which never uses the UBO rotation machinery (zero `GetStructuredBuffer` calls in `engine/Physics/`).
- Per-dispatch constants ship as CPU-written SSBOs: `XpbdUniforms` / `DummySolverUniforms` (gravity + dt), `DetectorConfig`, `GridConfig`, `ShapeSlotCount`, count buffers, `ScanParams` / `RadixSortParams`, and constant `ElemCount` buffers (`gpu_const_256` = 256, `gpu_one` = 1, `gpu_grid_cells_p1` = grid+1). All writes happen in `PreGPUStep`, which runs after the render frame's `StartFrame` fence — an implicit, render-owned safety guarantee.
- `ParallelScan` / `RadixSort` maintain acquire/reset parameter-buffer pools to serve multiple per-frame dispatches with different parameters.
- `XPBDGpuSolver` has two divergent dispatch paths: an impl-local `Dispatch` helper binding `GetDescriptorSet(0)` directly, and a `dispatch` lambda going through `BindComputeResource(cb, stage, binding, frame)`.
- `detect_collisions.comp` declares `DetectorConfig` as a true `uniform` block; `ComputeResourceBinding::UpdateGPUInfo` re-binds it to an unwritten `IndexedBuffer`, overriding the C++-bound `gpu_detector_config` — `contact_margin` is silently always 0.
- Scene data (positions, velocities, joints) uploads via `SubmissionHelper::EnqueueBufferSubmission` (staging + copy), not host writes; it is untouched by this change.

## Goals / Non-Goals

**Goals:**
- Remove all render-frame concepts from physics internals: single-slot bindings, no frame counters, no per-frame CPU writes to GPU memory, no parameter pools.
- Move all small per-dispatch parameters to push constants recorded at command-buffer time, with per-shader minimal blocks.
- Fix the latent `DetectorConfig.contact_margin == 0` bug.
- Make physics a pure command-buffer recorder: `GPUStep(cb)` binds, pushes, dispatches. Nothing more.

**Non-Goals:**
- Physics-independent submission (own command buffer / queue / sync) — command-buffer provisioning, submission and synchronization remain external (renderer/main-loop) responsibilities.
- Changing scene-data upload (staging path) or GPU-written SSBOs.
- Any render-side behavior change; `rhi-push-constants` is additive (size 0 → no range).

## Decisions

### D1: `SPLayout` reflects push-constant block size

`SPLayout` gains `uint32_t push_constant_size = 0`. `SPLayout::Reflect` appends a loop over `compiler.get_shader_resources().push_constant_buffers`, taking `get_declared_struct_size(type)` and keeping the max (SPIRV-Cross folds a stage's push constants into one resource; max guards exotic multi-block cases). Rationale: `Reflect` is the shared Rhi reflection entry point (used by `ComputeStage` and `MaterialLibrary`); a single field is a pure additive change — existing callers see 0. Alternatives: reflecting in `ComputeStage` locally — rejected: duplicates reflection logic and denies render-side future use.

### D2: `ComputeStage` declares the push-constant range

`CreatePipeline` builds `pc_ranges` from `layout.push_constant_size` (empty when 0) and passes it as the third `PipelineLayoutCreateInfo` argument; `GetPushConstantSize()` exposes the value. Range is `{eCompute, 0, size}`; stage flag is fixed because `ComputeStage` is compute-only.

### D3: Template `PushConstants` helper; `BindComputeResource` slot defaults to 0

```cpp
template <typename T>
void PushConstants(vk::CommandBuffer cb, ComputeStage &stage, const T &value) {
    assert(sizeof(T) <= stage.GetPushConstantSize());
    cb.pushConstants(stage.GetPipelineLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(T), &value);
}
```

Rationale: template deduces size, making single-parameter pushes `PushConstants(cb, stage, elem_count)` (no struct, no `&`/`sizeof`), and multi-parameter pushes use a local struct defined at the call site. Alternatives: `void* + size` — rejected: verbose at 30+ call sites, weaker type safety. `BindComputeResource`'s `slot` gains default `0`, symmetric with `AllocateResourceBinding`'s default `slot_count = 1`.

### D4: Per-shader minimal push blocks — no component-level unification

Every shader declares only the fields it consumes; C++ constructs the value inline at the dispatch site (scalar for one field, function-local struct for several). `memset_uint.comp` uses one push contract `{ uint elem_count; }`, shared by `SpatialHashBroadDetector`, `RadixSort` and `CompactUnique` (all three counts are CPU-known). `copy_uint.comp` keeps an SSBO `ElemCount` because `CompactUnique` binds the GPU-written `PairCount` (unknown at record time); `SpatialHashBroadDetector` uses a new push-constant twin `copy_uint_push.comp` for its CPU-known counts. Rationale: blocks are tiny; Vulkan requires `size <= stage range`. `radix_prefix_sum_256.comp` has no parameters and stays untouched.

### D5: Constant SSBOs and parameter pools are deleted, not kept

All 16 CPU-written constant buffers (including `gpu_const_256`, `gpu_one`, `gpu_grid_cells_p1` — all are `ElemCount` values) are deleted with their `Ensure*`/`GetVMAddress` write sites. Values move to CPU members (e.g. `GridConfig` already cached in `m_impl->grid_config`; grid→GPU-layout conversion cached at `Configure` time) and are pushed at record time. `ParallelScan`/`RadixSort` pools (`param_pool`, `Acquire*Param`, `ResetParamPool`) are deleted; each dispatch constructs and pushes its own value. Rationale: these exist only to serve per-frame CPU writes; push constants make them dead weight, and deleting them also removes the last cross-frame buffer-reuse hazard.

### D6: GPU-written SSBOs remain

`TotalAssignments`, `GlobalCount`, `PairCount`, `CollisionCount`, atomic counters and all scene buffers stay descriptor-bound — they are producer-consumer data, not constants.

### D7: C++ push layouts are compile-time checked

Each push struct carries `static_assert` on its expected size. The expected size is the shader's **declared** block size (std430 member layout): `get_declared_struct_size` does not add struct-level 16-byte tail padding, so `{ vec4; uint }` reflects 20 and `{ vec4; ivec4; uint }` reflects 36 — the C++ structs use matching natural alignment with `vec4` members first (glm::vec4 aligns to 4, so a scalar preceding a `vec4` would diverge from std430 offsets). `shader_refl_test` locks the reflection side (D10), including mixed-layout cases.

### D8: Binding renumbering is the final, gated step

Removing constant SSBOs leaves binding holes (e.g. `detect_collisions.comp` binding 11/12). Renumbering to consecutive `0..N` happens as the last task, after a user review gate on the preceding diff. Rationale: the first step must be reviewable as pure mechanism change; renumbering is a mechanical sweep better isolated and verified on its own.

### D9: `DetectorConfig` fix is an expected behavior change

Migrating `DetectorConfig` to push constants makes `contact_margin` effective (currently constant 0 due to the `IndexedBuffer` override). Accepted as intended behavior change; observed during manual physics validation.

### D10: Verification layers

1. `shader_refl_test`: embedded GLSL with a push-constant block, assert `push_constant_size` (e.g. 16 for `{ vec4 }`, 4 for `{ uint }`).
2. `headless_compute_test`: embedded GLSL reading a push constant (`output = input + params.offset`), record + submit + `waitIdle` + read-back assertion — exercises range declaration, helper, and runtime path end-to-end.
3. Full `ctest --preset debug` (48 tests) stays green; physics behavior validated manually via `physics_example` (no automated physics GPU test exists and none is added — user-validated).

## Risks / Trade-offs

- [Silent numeric corruption from wrong reflected size or C++/GLSL layout drift] → D7 `static_assert`s on every push struct + D10 test layers 1 and 2; `PushConstants` debug assert as backstop.
- [A shader misses migration and reads a deleted SSBO] → validation-layer descriptor errors at runtime; grep acceptance in tasks (`AllocateResourceBinding(3)`, `m_frame_counter`, constant buffer names = 0 hits); manual example run.
- [Contact behavior changes due to the `DetectorConfig` fix] → accepted (D9); margin default is small (0.001); `max_contacts` caps exist.
- [Multi-block push reflection edge cases] → max-size aggregation in `Reflect`; physics shaders each declare exactly one block.
- [Debug-only asserts (NDEBUG skips)] → sizes are compile-time constants checked by `static_assert`; runtime asserts are a second, debug-only net.

## Migration Plan

1. Rhi infrastructure: `SPLayout` field + reflection, `ComputeStage` range + getter, `PushConstants` helper, `BindComputeResource` slot default.
2. Shader migration: replace constant SSBO declarations with per-shader `layout(push_constant)` blocks (binding holes left in place).
3. C++ migration per component: default `slot_count`, remove frame counters, delete constant buffers and write sites, delete pools, construct-and-push at dispatch sites, unify XPBD dispatch paths.
4. Tests: extend `shader_refl_test` and `headless_compute_test`.
5. Build + full `ctest`; grep acceptance list zero hits.
6. **Review gate**: user reviews the diff; on approval, commit.
7. Final step: renumber shader bindings to consecutive 0..N (user reconfirms before this step).

## Open Questions

- Archiving `decouple-rhi-from-frame-rotation` (verified green, not archived): whether to archive before or after this change — user decision, independent of this change's implementation.
