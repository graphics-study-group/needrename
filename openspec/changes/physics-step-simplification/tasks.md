## 1. Dispatch geometry follows the logical count

- [ ] 1.1 Fix `SpatialHashBroadDetector::DispatchClear` so its workgroup count is derived from the `elem_count` argument it already receives rather than from the target buffer's size. Verify: a clear recorded over a range smaller than the target's capacity dispatches `ceil(elem_count / 64)` workgroups, and the existing broad-phase tests pass unchanged.
- [ ] 1.2 Fix `SpatialHashBroadDetector::DispatchCopy` the same way, using the destination's logical count rather than `dst.GetSize()`. Verify: a copy recorded over a logical range smaller than the destination's capacity dispatches workgroups for the logical count only, and the existing tests pass unchanged.
- [ ] 1.3 Document `ComputeBufferTyped<T>::GetCount()` as reporting capacity rather than a logical element count, and note that no engine caller may derive a bound from it. Verify: the header states the rule and a search of `engine/` finds no caller that uses it as an element bound.

## 2. Grow-only geometric capacity

- [ ] 2.1 Introduce the shared sizing helper implementing `new_bytes = max(needed, current * 2)` with no shrink, and document `GetSize()` as capacity. Verify: a unit test sizes a buffer up twice and down once, observing exactly two allocations and a capacity that never decreases.
- [ ] 2.2 Convert `PhysicsScene`'s SoA and joint buffer sizing to the shared helper. Verify: a headless fixture that alternates its shape and body counts between steps makes no allocation when the counts stay within the capacity already reached.
- [ ] 2.3 Convert `XpbdGpuSolver::Impl::EnsureBuffer` and `EnsureReduceGroup` to the shared helper. Verify: repeated steps at the same geometry allocate nothing after the first step, observable through the retirement facility's counters or allocator instrumentation.
- [ ] 2.4 Convert `SpatialHashBroadDetector::EnsureBuffer` / `EnsureAllBuffers` and `ConvexCollisionDetector::EnsureBuffers` to the shared helper, replacing the grow-only special cases that already existed so all buffers follow one rule. Verify: a run whose pair capacity oscillates below its high-water mark allocates nothing after the high-water mark is reached.
- [ ] 2.5 Confirm the algorithm sizing helpers remain capacity-agnostic: `RadixSort` and `SumByKey` accept a buffer larger than the geometry requires. Verify: a headless test records a sort and a reduction with deliberately oversized scratch and observes identical results to an exactly sized run.

## 3. CPU-known counts become push constants

- [ ] 3.1 Add the push-count variant of the counted entry-value clear alongside the existing buffer-count form, and correct the buffer-count form's header comment so it states the GPU-produced case only. Verify: both shaders compile, each declares only the bindings it binds, and a dispatch of either passes the binding-validation check.
- [ ] 3.2 Give `SumByKey` a count source that is either a CPU-known value or a bound buffer, selecting the corresponding kernel, and extend its push-constant block accordingly. Verify: the existing `gpu_sum_by_key` headless test passes with the buffer-count source, and a new case with a CPU-known value equal to the capacity produces the same result with no count buffer bound.
- [ ] 3.3 Move the hinge and fixed entry counts into the recorder's push-constant values and stop creating their host-visible count buffers. Verify: searching `engine/Physics/` for `SetConstantU32` and for a host-visible entry-count buffer finds neither, and the joint groups still clear and reduce correctly across a run that adds and removes joints.
- [ ] 3.4 Delete `SetConstantU32` and confirm no physics component publishes a parameter by writing buffer memory. Verify: the new *No CPU writes to GPU-visible memory outside command-buffer recording* scenarios hold, including a search for host-visible constant buffers in `engine/Physics/`.

## 4. Detector preparation moves onto the record path

- [ ] 4.1 Make `SpatialHashBroadDetector` prepare itself during `Record`: size its buffers, acquire its kernels and prepare its constants when the observed shape count or pair capacity changed, and do nothing when it did not. Verify: the first `Record` after binding to a scene sizes and acquires; a second `Record` at the same geometry performs no allocation, observable through allocation counters.
- [ ] 4.2 Make `ConvexCollisionDetector` do the same, taking its pair buffers from the broad detector's current output at preparation time rather than from a reference cached at a previous step. Verify: a run that grows the pair capacity between steps still reads the broad detector's live pair buffers, under the validation layer.
- [ ] 4.3 Remove both `Configure` methods and the ordering assertion that guarded them. Verify: `engine/Physics/` declares no `Configure` on a detector and no `Record` precondition on a prior configure call; the solver records a complete step with no preparation call.
- [ ] 4.4 Confirm no pipeline is created between a call's first and last dispatch. Verify: instrument pipeline creation and run a step in which a buffer resize and a capacity change occur, observing that all creation happens before the first dispatch of the affected `Record` call.

## 5. Lifecycle collapse

- [ ] 5.1 Move `XpbdGpuSolver::PreGPUStep`'s remaining work — kernel acquisition, buffer sizing at the record site, per-dispatch constants, detector preparation — into `GPUStep`, and delete the method. Verify: a freshly constructed solver records a complete step on its first `GPUStep(cb)` with no other call, and the physics app runs a scene to rest.
- [ ] 5.2 Delete `PreGPUStep` and `PostGPUStep` from `ISolver`. Verify: the interface declares `OnBindToScene`, `GPUStep` and `IsInitialized` only, and searching `engine/` and `app/` finds no override or partial implementation of either deleted method.
- [ ] 5.3 Delete `PhysicsSystem::PreGPUStep` / `PostGPUStep` and their loops, and update every call site: `MainClass::RunOneFrame`, the editor loop, `PhysicsApp`'s seed and step paths, and the editor-run-game example. Verify: the project builds in both configurations and each call site records physics through a single `GPUStep(cb)`.
- [ ] 5.4 Update `DummySolver` to acquire its kernel and prepare its constants inside `GPUStep`. Verify: the DummySolver scenarios hold, including that a second `GPUStep` acquires nothing.
- [ ] 5.5 Re-check the barriers that the deleted phase used to imply. Verify: no barrier was recorded only at a phase boundary, and the physics app's stability run reports no validation error about a missing dependency.

## 6. Count buffers become device-local

- [ ] 6.1 Allocate `gpu_total_assignments`, `gpu_global_count`, `gpu_pair_count` and `gpu_unique_count` without CPU access. Verify: the four buffers are created device-local, and a search of the detector and the solver confirms no `GetVMAddress` or `Invalidate` call reaches them.
- [ ] 6.2 Confirm the broad-phase results are unchanged. Verify: the headless broad-phase test and the physics app's stability run produce the same pair counts and contacts as before the change.

## 7. Verification

- [ ] 7.1 Run the full physics suite in both configurations. Verify: `cmake --build --preset msvc-debug` then `ctest --preset msvc-debug` pass, and the same for the release configuration.
- [ ] 7.2 Run a long dynamic fixture that repeatedly grows and shrinks the shape and body counts across several frames in flight. Verify: it runs under the validation layer with no use-after-free or invalid-handle report, and the allocation count grows logarithmically in the geometry high-water mark rather than with the number of changes.
- [ ] 7.3 Confirm the declared capability deltas cover the swept capabilities. Verify: `openspec validate physics-step-simplification --strict` reports the change valid, and the delta set includes `gpu-parallel-scan`, `gpu-convex-collision-detection`, `spatial-hash-broad-phase` and `xpbd-solver-multi-rg` alongside the capabilities this change modifies for its own reasons.
