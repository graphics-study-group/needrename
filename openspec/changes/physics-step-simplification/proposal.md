## Why

The physics GPU pipeline pays for a three-phase lifecycle (`PreGPUStep` → `GPUStep` → `PostGPUStep`) that exists only to work around a capability the RHI did not have: it was not safe to allocate or resize a buffer while a command buffer was being recorded. `XpbdGpuSolver::PreGPUStep` (`XPBDGpuSolver.cpp:414-571`) is 158 lines whose entire job is to size buffers before recording; `GPUStep` then only derives dispatch geometry, and its own comment says so — *"Everything capacity-dependent was sized in PreGPUStep"* (`:625-627`). Every buffer size it computes is a value the CPU already holds at record time (`body_count`, `shape_count`, `all_pairs`), so the phase buys nothing but indirection.

The same workaround has three other costs that survive today:

- **Churn.** All five copies of `EnsureBuffer` recreate on *any* size difference (`if (!buf || buf->GetSize() != bytes)`), so an oscillating shape or body count reallocates roughly thirty buffers per change instead of only when a capacity step is crossed.
- **CPU writes to GPU-visible memory during recording.** The only such writes in `engine/Physics` are `SetConstantU32` (`XPBDGpuSolver.cpp:260-263`, called at `:506` and `:531`) for the hinge and fixed entry counts — values the CPU knows, routed through a host-visible buffer that a previous submission may still be reading.
- **A second lifecycle to keep synchronized.** Detectors carry a `Configure` step whose only reason to exist is that buffer allocation had to happen outside recording (`detector-configure-detect`), and callers must remember to run it before `Record` (`SpatialHashBroadDetector.cpp:674` asserts it).

`gpu-buffer-retirement` removes the blocker: retired allocations are parked under a submission epoch and released after the submission that may reference them completes. With that in place, allocation during recording is safe, and the phase, the `Configure` step, and the pre-recording count uploads all lose their reason to exist.

## What Changes

- **Delete `PreGPUStep` / `PostGPUStep`.** `ISolver` collapses to `OnBindToScene` / `GPUStep(vk::CommandBuffer)` / `IsInitialized`, and `PhysicsSystem` drops both forwarding loops. `PostGPUStep` has **no override anywhere in the engine**, so deleting it costs nothing; `XpbdGpuSolver::PreGPUStep`'s four jobs each move to the record site:
  1. lazy shader loading and stage/binding creation → on first use at record time;
  2. buffer sizing → at the point where the size is known;
  3. push-constant values → already computed at record time;
  4. detector `Configure` → folded into the detector's record path.
- **BREAKING: two existing requirements are deliberately overturned.** `xpbd-solver-multi-rg`'s *"`GPUStep` SHALL NOT allocate any GPU resources"* and `physics-push-constants`'s *"`PreGPUStep` ... SHALL NOT write to GPU-visible memory"* both existed only because record-time allocation and record-time writes were unsafe. Both are removed, with the reversal and its precondition (`gpu-buffer-retirement`) stated in the delta.
- **Grow-only, geometric buffer capacity.** The shared sizing rule becomes `new_bytes = max(needed, current * 2)` and never shrinks, so capacity tracks capacity steps rather than every value change. `GetSize()` is documented and treated as **capacity, not logical element count**. The two call sites that derive dispatch geometry from capacity rather than the logical count they already receive — `SpatialHashBroadDetector::DispatchClear` (`:314`) and `DispatchCopy` (`:326`) — are fixed in the same change, because they would otherwise overshoot after a grow.
- **CPU-known counts become push constants.** The hinge and fixed entry counts move into the push-constant block; only the contact group's count stays a bound buffer, because that one is produced by an earlier GPU pass. Net: three host-visible count buffers become one, and `SetConstantU32` disappears. The counted clear gains a push-constant variant (the repo's existing `copy_uint.comp` / `copy_uint_push.comp` split is the precedent), and `SumByKey` accepts its count either as a value or as a bound buffer.
- **Detector configuration folds into recording.** `ConvexCollisionDetector::Configure` and `SpatialHashBroadDetector::Configure` become internal preparation performed on the record path when the geometry they depend on changes, so a caller no longer orchestrates a configure step before `Record`.
- **Unused host-visible count buffers become device-local.** `gpu_total_assignments`, `gpu_global_count`, `gpu_pair_count` and `gpu_unique_count` are created host-visible but are never read or written by the CPU, while all four are bound in hot kernels.
- No compute dispatch API change and no change to the descriptor-set or retirement mechanisms: both belong to `gpu-buffer-retirement` and `compute-kernel-dispatch`.

## Capabilities

### New Capabilities

- `rhi-buffer-capacity`: the growth **policy** of the reallocation entry points that `stable-buffer-identity`'s `rhi-buffer-reallocation` capability introduces — capacity only grows, growth through `EnsureCapacity` is geometric, `Reallocate` keeps its exact-size semantics, and `GetSize()` reports capacity rather than logical element count, so every logical bound must be supplied explicitly by the caller. It restates the policy of that contract rather than introducing a rival mechanism or entry point.

### Modified Capabilities

- `physics-solver-interface`: `ISolver` loses `PreGPUStep` / `PostGPUStep`; `PhysicsSystem` loses its two forwarding entry points; ordering requirements that refer to the three-phase dispatch are restated for the single-call lifecycle.
- `xpbd-solver-multi-rg`: the "`GPUStep` SHALL NOT allocate any GPU resources" prohibition is **removed**, and the `PreGPUStep handles CPU-side preparation` requirement is replaced by record-time preparation.
- `physics-push-constants`: the "no CPU writes to GPU memory during `PreGPUStep`" requirement is **removed** (the writes themselves are eliminated); the existing "CPU-known counts go to push constants" rule is extended to the hinge/fixed entry counts and to the counted clear, which finally makes it hold without exception.
- `physics-main-loop-integration`: the main loop's physics order drops `PreGPUStep` and `PostGPUStep`.
- `editor-physics-pipeline`: the editor's play-mode pipeline drops the two phases.
- `headless-present-pipeline`: the requirement that mentions `physics->PostGPUStep` as post-`CompleteFrame` CPU work is restated without it.
- `physics-dummy-solver`: `DummySolver` no longer overrides `PreGPUStep`; its shader/binding initialization moves into the record path.
- `xpbd-contact-solve`: the clause placing detector `Configure` in `PreGPUStep` is restated, and the counted-clear requirements admit a CPU-known-count variant alongside the GPU-produced one.
- `detector-configure-detect`: `Configure` stops being a caller-visible phase; buffer sizing, count uploads and binding creation happen on the record path when their inputs change.
- `physics-gpu-shaders`: a push-constant variant of the counted clear joins the shader inventory, and the loader no longer runs from a pre-recording phase.
- `gpu-sum-by-key`: `Record`'s entry-count input becomes a count source that may be either a CPU-known value or a bound buffer.
- `spatial-hash-broad-phase`: dispatch geometry for the clear and copy passes derives from the logical element count rather than buffer capacity; the four count buffers that no CPU code reads become device-local; the detector's configure phase folds into recording and its requirement text is brought onto the current record-based detector contract.
- `gpu-parallel-scan`: the requirements describing the removed render-graph `AddPasses` API, its per-pass parameter buffers and its render-graph dependency declarations are removed; construction, buffer bindings, location independence and the broad-detector migration are restated against the record-based contract.
- `gpu-convex-collision-detection`: the narrow-phase detector is restated against the record-based contract — no self-owned render graph, preparation on the record path, and the pair buffers consumed as plain bound inputs.

## Impact

**Code**
- `engine/Physics/Solver/ISolver.h`, `PhysicsSystem.{h,cpp}`, `Solver/XpbdGpuSolver.{h,cpp}`, `Solver/DummySolver.{h,cpp}` — lifecycle collapse and record-time preparation.
- `engine/Physics/PhysicsScene.{h,cpp}` — grow-only sizing shared by the scene's SoA columns.
- `engine/Physics/Collision/SpatialHashBroadDetector.{h,cpp}`, `Collision/ConvexCollisionDetector.{h,cpp}` — configure folds into recording, dispatch geometry from the logical count, device-local count buffers.
- `engine/Physics/gpu_algorithm/SumByKey.{h,cpp}` — count-source input.
- `engine/Physics/shader/solver/XPBDSolver/clear_entry_values*.comp` — the push-constant clear variant.
- `engine/Framework/MainClass.cpp`, `app/physics/PhysicsApp.cpp`, `example/editor_run_game_example/main.cpp`, the editor pipeline — call-site updates.
- `engine/Rhi/Buffer/` — the capacity contract and the shared sizing helper.

**API**
- Breaking: `ISolver` loses two virtual methods; `PhysicsSystem` loses `PreGPUStep` / `PostGPUStep`; `SumByKey::Record`'s entry-count parameter type changes. `Configure` leaves the detectors' public surface.

**Dependency**
- Requires `gpu-buffer-retirement` (record-time reallocation must be retire-safe), `stable-buffer-identity` (whose `rhi-buffer-reallocation` contract this change's `rhi-buffer-capacity` policy modifies, and which owns the exact-size/grow-only split this change makes geometric) and builds on `compute-kernel-dispatch` (dictionary dispatch is what makes record-time sizing readable). All are referenced by name only; this change does not depend on their exact wording.

**Non-goals carried into the design**
- Slot reuse and physics delete support remain future work: `AllocateRigidBodySlot` / `AllocateCollisionShapeSlot` still never reuse a slot, so contact buffers stay quadratic in the slot high-water mark and do not fall back when shapes are deleted. Grow-only fixes churn, not growth.

**Tests**
- The physics app's stability test and a long dynamic run must show a flat memory profile across repeated grow/shrink of the shape and body counts, and the existing headless algorithm tests must pass unchanged apart from the count-source parameter.
