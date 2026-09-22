# Design — physics-step-simplification

See `proposal.md` — Why for the motivation. This document records the decisions that shape the collapse, the prerequisites that must land with it, what it deliberately overturns in its sibling changes, and what it leaves out.

## Context

### The phase this change deletes

`ISolver` declares five methods and `PhysicsSystem` forwards three of them per scene. `XpbdGpuSolver::PreGPUStep` (`XPBDGpuSolver.cpp:414-571`) performs four jobs before recording:

1. lazy shader loading and stage/binding creation (`:265-332`);
2. sizing every capacity-dependent buffer (`:427-531`);
3. computing push-constant values (`:533-536`);
4. constructing and configuring both detectors (`:539-569`).

`GPUStep` then only derives dispatch geometry — its own comment says so at `:625-627`.

The phase exists for exactly one reason: it was not safe to allocate or resize a GPU buffer while a command buffer was being recorded (`gpu-buffer-retirement` establishes that it is not safe because `BufferAllocation` frees immediately and nothing tracks in-flight work). Once that is fixed, the phase is work that happens earlier than it needs to.

### The load-bearing fact

Every input to that preparation is a value the CPU already holds at record time: `body_count` and `shape_count` come from the scene's host columns, `all_pairs = shape_count * (shape_count - 1) / 2` is arithmetic on a host value, and the joint counts are host container sizes. Nothing in the preparation needs a number produced on the GPU. If any of it did, moving it to the record site would be impossible and this change would have to keep the phase.

### Cross-change baseline convention

Three changes touch overlapping capabilities. This document and its deltas follow this rule:

> For a capability that `gpu-buffer-retirement` or `compute-kernel-dispatch` also modifies, a `## MODIFIED Requirements` block is based on the **most recent delta text** (`openspec/changes/gpu-buffer-retirement/specs/<cap>/spec.md`, then `openspec/changes/compute-kernel-dispatch/specs/<cap>/spec.md`), not on `openspec/specs/<cap>/spec.md`.

Where a requirement keeps its name, the block also reproduces every scenario name the current text carries, because a `MODIFIED` block replaces the whole requirement and archiving refuses to drop a scenario. Where a requirement changes name, the rename is declared in `## RENAMED Requirements`. This is why several blocks below read as a layer over the sibling changes' wording rather than over the archived spec.

### Capabilities swept for the sibling changes

`compute-kernel-dispatch` deletes `ComputeStage` / `ComputeResourceBinding` / `ComputeHelpers`, which nominally touches many capabilities. It applied the rule "modify where the requirement block is otherwise current, defer where it is stale for reasons another change already owns", and deferred four to this change, which rewrites them anyway:

- `gpu-parallel-scan`
- `gpu-convex-collision-detection`
- `spatial-hash-broad-phase`
- `xpbd-solver-multi-rg`

**These four are stale for two independent reasons and the history must not be misattributed.** The first is the already-archived removal of the render graph from the physics GPU pipeline, which left requirements describing `AddPasses(RenderGraphBuilder&, RGBufferHandle, ...)`, `Detect(cb)`, `Configure(cb)` and `UseBuffer` declarations that no longer exist. The second is `compute-kernel-dispatch`'s type deletion. This change is not the cause of either; it moves the text onto the current record-based contract because it is already rewriting those blocks for its own reasons. Where a requirement's whole subject is gone rather than merely mis-worded, it is removed with a Reason and a Migration rather than rewritten.

### Corrections made while rewriting, and scope discipline

Several blocks were factually wrong before this change. Because a `MODIFIED` block replaces the whole requirement, leaving them in place would re-assert them, so they are corrected and recorded here:

- `physics-solver-interface`'s `XpbdGpuSolver` requirement and `xpbd-solver-multi-rg`'s constructor text both describe a `RenderSystem &` constructor; the constructor is `(const Rhi::DeviceInterface&, const Rhi::AllocatorState&)`.
- The same two requirements describe solver-owned render graphs, which the archived render-graph removal deleted.
- `gpu-convex-collision-detection` places the narrow-phase SPIR-V under `solver/ConvexCollisionDetector/`; it is `collision/ConvexCollisionDetector/detect_collisions.comp.spv`.
- The `Detect(...)` spelling used by the older detector requirements is `Record(...)` in the current contract.

**Pre-existing debt repaired in passing:** `physics-main-loop-integration`'s `Physics pipeline in RunOneFrame` requirement had **no scenarios at all**, so the capability already failed `--strict` before this change. The rewritten block supplies two, which repairs that failure. It is not a behaviour this change broke.

**Pre-existing debt deliberately left alone:** `physics-main-loop-integration` and `editor-physics-pipeline` carry archiver placeholder `## Purpose` sections, and `physics-gpu-shaders` requirements 5 and 6 (*Shape world pose update shader*, *Quaternion multiplication helper*) have the same missing-scenario defect. A delta's `## Purpose` is read only when a capability is created, so it cannot replace an existing one, and those two shader requirements are unrelated to this change. All three are tracked separately; touching them here would misattribute unrelated churn.

### What this change overturns in its sibling changes

`compute-kernel-dispatch` deliberately preserved the two phases it did not need to remove, changing only what they do:

- `detector-configure-detect`: *"All compute kernels SHALL be acquired in `Configure`, so that `Record` creates no pipeline"*, and `Configure` remains a caller-visible method.
- `physics-dummy-solver`: *"`DummySolver::PreGPUStep()` SHALL perform the shader initialization ... `DummySolver::GPUStep(cb)` SHALL only dispatch."*

This change removes both phases, so those clauses are superseded on purpose (see D7). `compute-kernel-dispatch` also removes *Detector binding allocation in Configure*; this change relies on that removal and does not repeat it.

## Goals / Non-Goals

**Goals**

- Make the solver's step a single record call, with no caller-visible phase before or after it.
- Remove the machinery that existed only to size buffers ahead of recording, so capacity changes stop being a two-function protocol.
- Stop the CPU from publishing parameters by writing GPU-visible memory.
- Make detector preparation the detector's own business.

**Non-Goals (design-level)**

- No slot reuse, and therefore no change to how large the contact buffers get: capacity tracks the slot high-water mark and does not fall back when shapes are deleted. This is physics delete support and is future work. Grow-only fixes churn, not growth.
- No `DispatchIndirect` and no automatic barriers. Barrier placement stays manual; the dispatch surface inserts none.
- No change to the descriptor arena, the retirement queue, or the dispatch surface itself; those belong to `gpu-buffer-retirement`, `descriptor-arena-epoch-buckets` and `compute-kernel-dispatch`.
- No attempt to make the physics step frame-independent. The GPU-side serialisation across frames that physics relies on is provided by the frame submission's wait on the previous frame, and is unchanged here.

## Decisions

### D1: The phase dies because its justification died

`PreGPUStep` / `PostGPUStep` are removed from `ISolver` and `PhysicsSystem`, and their call sites are deleted. `PostGPUStep` costs nothing to remove — it has **no override anywhere in the engine**, and the only callers are the main loop, the editor, and `PhysicsApp`. `PreGPUStep`'s four jobs move to the record site, which D2 shows is possible.

**Alternative rejected:** keep `PreGPUStep` as an optional hook and merely stop calling it. It would leave a lifecycle contract that no caller honours and no test exercises, and the next solver author would have to work out which of the two paths is live.

**Alternative rejected:** keep the phase but make it lazy (call it from `GPUStep` on first use). That is what `Configure` folding does at the detector level (D7); doing it for the solver as well would keep two names for one thing.

### D2: Preparation moves to the record site, not to the GPU

Every size the solver computes is CPU-known at record time (see Context). Preparation therefore moves into `GPUStep`, at the point where the geometry is known, and needs no GPU round trip.

**Alternative rejected:** leave the sizes where they are but make resizing cheaper. It keeps the two-function protocol, the cross-function `capacity` plumbing, and the assertion that `GPUStep` must not allocate.

**Alternative rejected:** derive the sizes on the GPU and use indirect dispatch. That is the eventual answer to the capacity plumbing (a deferred item), but it cannot be the mechanism for buffers whose *CPU* side is a host column that must be uploaded.

### D3: Capacity only grows, geometrically, and size means capacity

The shared sizing rule becomes `new_bytes = max(needed, current * 2)`, never shrinking. This is specified as a reusable contract in the new `rhi-buffer-capacity` capability rather than as a physics detail, because every buffer consumer needs it: an oscillator workload currently reallocates roughly thirty buffers per geometry change, and after the change only when a capacity step is crossed.

**Alternative rejected:** keep exact-fit sizing and rely on the retirement queue to make the churn safe. Safe is not free: each reallocation costs an allocation, a descriptor-set mint against the new handle, and a retired allocation held until its epoch completes. Grow-only removes the churn instead of paying for it.

**Alternative rejected:** grow-only without geometric growth. A scene whose shape count rises by one per frame would still reallocate every frame.

### D4: The dispatch-geometry prerequisite lands in the same change

`SpatialHashBroadDetector::DispatchClear` (`:314`) and `DispatchCopy` (`:326`) derive workgroup counts from `GetSize()` while already receiving the logical `elem_count` as a parameter. Under exact-fit sizing the two agree; under grow-only they do not, and each pass would launch workgroups for elements beyond the logical count. The shaders bound their writes by `elem_count`, so the result stays correct — the cost is wasted dispatches, which is exactly the kind of silent regression that would be attributed to grow-only rather than to the two call sites that ignored their own argument.

They are therefore fixed in the same change, and `ComputeBufferTyped<T>::GetCount()` — which computes the element count from the buffer size — is documented as having the same defect. It has no engine callers, so it is a comment rather than a behavioural fix.

### D5: CPU-known counts become push constants, and the count source picks the shader

The only CPU writes to GPU-visible memory in `engine/Physics` are `SetConstantU32` (`:260-263`), called twice for the hinge and fixed entry counts (`:506`, `:531`). Both counts are CPU-known: the host publishes `kJointSlotsPerJoint * joint_count` while the group's capacity is `kJointSlotsPerJoint * max(1, joint_count)`, so the count equals the capacity whenever a joint exists and is zero when none does. Only the **contact** group's count is produced on the GPU within the same substep, and only that one stays a bound buffer.

The shader side follows the convention the repository already applies in `copy_uint.comp` / `copy_uint_push.comp`: a CPU-known count travels in the push block, a GPU-produced count stays an SSBO, and the two cases are **separate shader files rather than a mode flag**. The counted entry-value clear therefore gains a push-count variant, and `SumByKey`'s entry-count input becomes a count source (`variant<uint32_t, const ComputeBuffer *>` in C++ terms) that selects its kernel.

**The constraint that forces the split:** a kernel must never declare a descriptor binding it does not bind, because the dispatch surface rejects a missing binding (change C). A single shader that sometimes reads the count from a binding and sometimes from the push block would have to declare the binding unconditionally and leave it unbound on the CPU-known path. That is why this is two shaders and not one with a flag — the same reason `copy_uint.comp` and `copy_uint_push.comp` are two files.

**Alternative rejected:** keep the hinge and fixed counts in host-visible buffers and rotate them per epoch. It preserves the CPU write, which is the thing being removed, and the counts have no reason to be in memory at all.

**Alternative rejected:** make the contact count a push constant too, by reading it back. Any readback is a CPU/GPU stall inside recording, and the count is not known until an earlier pass in the same command buffer has run.

### D6: `clear_entry_values.comp`'s header comment is corrected, not deleted

The shader's header (`:12-14`) states that the entry count "is produced on the GPU in the same substep ... and cannot be a push constant". That is true of the contact group and false of the hinge and fixed groups. The comment is corrected on the buffer-count form to say which case it serves, and the push-count form carries its own header. This is recorded as a decision because the sentence reads as a general rule and would otherwise be copied into the new variant.

### D7: Detector configuration folds into recording, superseding `compute-kernel-dispatch`'s clause

`Configure` leaves both detectors' public surface. A detector prepares itself during `Record` — sizing its result buffers, acquiring its kernels, preparing its per-dispatch constants — when the geometry it observes has changed since the previous call, so the solver's step needs no configure step and the ordering assertion at `SpatialHashBroadDetector.cpp:674` disappears along with the state it guarded.

This **supersedes** `compute-kernel-dispatch`'s requirement that kernels be acquired in `Configure`, and its `physics-dummy-solver` text that keeps `PreGPUStep` as the initialization site. The reversal is deliberate: the property worth keeping from those clauses is *no pipeline is created in the middle of a call's dispatch sequence*, and that is preserved by requiring preparation to complete before the call's first dispatch. What is dropped is the claim that the acquisition site must be a different call, which existed to keep pipeline creation out of a lifecycle phase that no longer exists.

**Alternative rejected:** keep `Configure` as an internal method the solver calls at the top of `GPUStep`. It satisfies the letter of both changes but leaves two ways to prepare a detector, keeps the "did you configure it?" failure mode reachable from any future caller, and leaves `Record` unable to prepare itself when the geometry changes mid-step (which happens: the narrow detector's pair capacity tracks the broad detector's output, and both are recomputed per step).

**Residual staleness, recorded:** `xpbd-contact-solve`'s and `gpu-convex-collision-detection`'s scenario names still say "Detect" and "render graph" in places, because a `MODIFIED` block must reproduce existing scenario names verbatim. Their bodies are current; the titles are not, and renaming them is not possible from a delta.

### D8: Count buffers no CPU code touches become device-local

`gpu_total_assignments` (`:182`), `gpu_global_count` (`:221`), `gpu_pair_count` (`:210`) and `gpu_unique_count` (`:242`) are created with CPU access but are never read or written by the CPU — verified by searching the detector and the solver for `GetVMAddress` and `Invalidate`, which find only `SetConstantU32`. All four are bound in hot kernels, where host-visible memory may cost bandwidth. They become device-local.

**Note:** this is not a synchronisation fix. These buffers were never a race, because the CPU never touched them. The change is about where the memory lives.

### D9: Storage reuse is the caller's responsibility, including its barrier

Temporary storage is reused across calls and grows only when a call's geometry exceeds it (D3). Two recordings that share one storage set therefore have an execution-order dependency through it, and the barrier that dependency needs is the **caller's**, recorded explicitly, consistent with the pipeline's no-automatic-barriers rule. Recordings that use different storage sets are independent for storage reasons, so a caller that wants two reductions interleaved without a storage barrier supplies two sets and accepts the duplication.

This is stated once in `gpu-sum-by-key` and documented in the algorithm headers, because the failure mode is silent: a missing barrier produces a wrong sum with no error, and the reuse makes it reachable where a per-call allocation would not have.

**Alternative rejected:** allocate per call to make the barrier unnecessary. It reintroduces the churn D3 removes, and the retirement queue makes it *safe* but not free.

### D10: `GetResultBuffers()`' stability is preserved deliberately

The detectors return raw `ComputeBuffer*` and the solver caches the broad detector's pair buffers into the narrow detector. Retiring an allocation keeps the memory alive, but the `ComputeBuffer` facade is destroyed with the owning `unique_ptr`, so a pointer cached across a resize would dangle even though the memory would not. The pair buffers are therefore only re-pointed when their capacity actually changes, and the narrow detector's cached reference is refreshed from `GetResultBuffers()` at preparation time (D7) rather than held from a previous step.

## Risks / Trade-offs

- **[Record-time reallocation is only safe because of a sibling change]** → Mitigation: this change depends on `gpu-buffer-retirement`; the ordering is stated in the proposal, and a build without the retirement queue would reintroduce the use-after-free the phase was avoiding. The verification tasks run under the validation layer with two to three frames in flight.
- **[Grow-only silently changes dispatch geometry]** → Mitigation: D4 fixes the two call sites that derive geometry from capacity in the same change, and the new capability states the rule; the search for `GetSize()` uses that feed a dispatch or an element bound is part of the verification.
- **[A count-source split doubles a shader]** → Mitigation: the split follows an existing, documented convention (`copy_uint.comp` / `copy_uint_push.comp`) and the shared body can be factored into a GLSL include, which the repository already uses for `common/` and `support.glsl`. The alternative — one shader with an unbound binding — is rejected by the dispatch contract.
- **[Removing `Configure` leaves detector preparation untested in isolation]** → Mitigation: the preparation path is exercised on every solver step, and the "no-op when nothing changed" case is a scenario with an observable assertion (allocation counters do not move between two identical steps).
- **[Deleting `PreGPUStep` removes the only place a solver could prepare outside recording]** → Mitigation: this is intended, and the record-time path is the only one that stays correct when geometry changes mid-step. If a future solver needs a genuinely expensive one-time setup, lazy acquisition on first use covers it without a phase.
- **[Reported size no longer equals element count]** → Mitigation: the rule is a named capability with its own scenarios, and D4 removes the two places in physics that relied on the old equality.
- **[Sibling changes' clauses are reversed here, so the archive order matters]** → Mitigation: the reversals are stated in the delta that performs them (D7, and the removed requirements in `physics-push-constants`), and the cross-change baseline convention is stated above so a future reader can tell which text each block was based on.

## Migration Plan

1. **Fix the dispatch-geometry prerequisite (D4).** Change the two helper dispatches to size from `elem_count`, and document `GetCount()`. This is behaviour-preserving under exact-fit sizing and must land before grow-only.
2. **Introduce grow-only geometric sizing (D3)** behind the shared sizing helper, and convert the five `EnsureBuffer` copies and the scene's SoA sizing to it. Verify with the existing tests before touching the lifecycle.
3. **Move the CPU-known counts to push constants (D5, D6):** add the push-count clear variant, give `SumByKey` its count source, extend the push blocks, and delete `SetConstantU32` and the two count buffers.
4. **Fold detector configuration into recording (D7)** and delete the `Configure` surface, together with the narrow detector's cached pair-buffer plumbing (D10).
5. **Collapse the lifecycle (D1):** move `PreGPUStep`'s remaining work into `GPUStep`, delete both phases from `ISolver`, `PhysicsSystem`, the main loop, the editor and `PhysicsApp`, and delete `Configure` call sites.
6. **Make the unused count buffers device-local (D8).**
7. **Verification:** the physics app's stability run and a long dynamic fixture, plus the full suite in both configurations.

Rollback: steps 1–2 and 6 are independently revertible (they change sizing and memory placement but not the lifecycle); steps 3–5 are the coupled core, and reverting them means restoring the phase, which is a mechanical revert of the same commits.

## Open Questions

- **Whether `SumByKey`'s shared body should be factored into a GLSL include** or duplicated between the two count-source variants. Deferred: it affects maintainability, not behaviour, and the answer depends on how the variant is written.
- **Whether the hinge and fixed reductions should be skipped entirely when their joint count is zero**, since the host knows the count is zero and the reduction is then a no-op over an unread group. Deferred: it is a dispatch-count saving with no contract change, and the current behaviour (entry count zero ⇒ no output) is already correct.
- **Whether the retired `Configure` should leave a thin alias** for external callers outside this repository. Deferred: no caller in the tree needs one, and an alias would keep the ordering failure mode reachable.
