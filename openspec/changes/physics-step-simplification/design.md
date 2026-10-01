# Design — physics-step-simplification

See `proposal.md` — Why for the motivation. This document records the decisions that shape the collapse, the prerequisites that must land with it, what it deliberately overturns in its sibling changes, and what it leaves out.

## Context

### The phase this change deletes

`ISolver` declares six methods and `PhysicsSystem` forwards four of them per scene. `XpbdGpuSolver::PreGPUStep` (`XPBDGpuSolver.cpp:414-571`) performs four jobs before recording:

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

- `physics-solver-interface`'s `XpbdGpuSolver` requirement and `xpbd-solver-multi-rg`'s constructor text both describe a `RenderSystem &` constructor; the constructor is `(Rhi::DeviceContext&)`.
- The same two requirements describe solver-owned render graphs, which the archived render-graph removal deleted.
- `gpu-convex-collision-detection` places the narrow-phase SPIR-V under `solver/ConvexCollisionDetector/`; it is `collision/ConvexCollisionDetector/detect_collisions.comp.spv`.
- The `Detect(...)` spelling used by the older detector requirements is `Record(...)` in the current contract.
- `physics-dummy-solver`'s *DummySolver compute shader* requirement still declared a `ModelMatrices` binding and claimed the displacement shader writes a TRS model matrix. The model-matrices refactor removed that binding and moved the pose-to-matrix mapping into the shared shader, so the requirement is restated with the three buffers the shader actually binds.
- The blocks written before the model-matrices refactor omitted the scenarios it added to requirements this change already rewrites — `physics-main-loop-integration`'s model-matrix production paragraph and its three scenarios, `editor-physics-pipeline`'s ungated production and its *Stopped frame still produces matrices* scenario, `physics-solver-interface`'s four `GPUCalcModelMatrices` scenarios (dropped twice over, because a `RENAMED` block escapes the archiver's scenario check), `physics-dummy-solver`'s two, and `physics-gpu-shaders`' *Model matrix shader is loaded from the shared directory*. All are reproduced here, because a `MODIFIED` block replaces the whole requirement and the archiver refuses to drop a scenario.

**Pre-existing debt repaired in passing:** `physics-main-loop-integration`'s `Physics pipeline in RunOneFrame` requirement carried a single scenario (*Physics compute shares the frame command buffer*) before the model-matrices refactor added three more; the rewritten block keeps all four, so the earlier note claiming the requirement had no scenarios was wrong. `physics-app-pause` named the deleted phases in its *Seed does not run the step pipeline* scenario and had no delta at all, so this change adds one.

**Pre-existing debt deliberately left alone:** `physics-main-loop-integration` and `editor-physics-pipeline` carry archiver placeholder `## Purpose` sections; `detector-configure-detect`'s `## Purpose` describes the two-phase `Configure()` / `Record()` API this change deletes, so after archiving it will read as the opposite of the capability's own requirements; and `physics-gpu-shaders` requirements 5 and 6 (*Shape world pose update shader*, *Quaternion multiplication helper*) have the same missing-scenario defect. A delta's `## Purpose` is read only when a capability is created, so it cannot replace an existing one, and those two shader requirements are unrelated to this change. All four are tracked separately; touching them here would misattribute unrelated churn.

### What this change overturns in its sibling changes

`compute-kernel-dispatch` deliberately preserved the two phases it did not need to remove, changing only what they do:

- `detector-configure-detect`: *"All compute kernels SHALL be acquired in `Configure`, so that `Record` creates no pipeline"*, and `Configure` remains a caller-visible method.
- `physics-dummy-solver`: *"`DummySolver::PreGPUStep()` SHALL perform the shader initialization ... `DummySolver::GPUStep(cb)` SHALL only dispatch."*

This change removes both phases, so those clauses are superseded on purpose (see D7). `compute-kernel-dispatch` also removes *Detector binding allocation in Configure*; this change relies on that removal and does not repeat it.

D12 reverses a rule those sibling changes never wrote but always honoured, and which predates all of them: **every `gpu_algorithm` class takes its working buffers from the caller and allocates nothing.** Its origin is the render graph. `extract-gpu-parallel-scan` required the caller to *"import it into the render graph once, and pass the same buffer and handle to both `AddPasses` calls"*, and `broad-phase-dedup` restated the rule as the house pattern ("owns only shaders + small per-pass parameter buffers, all large working buffers caller-provided"). The graph needed the resource declared by the caller so it could track the dependency; with the render graph gone from the physics pipeline, what remains is a scratch pointer threaded through three call sites, five public sizing formulas and a set of size guards that exist to catch a caller who applied them wrongly. This change moves the ownership and leaves the barrier. It is a contract reversal in `gpu-radix-sort`, `gpu-parallel-scan`, `gpu-sum-by-key` and `gpu-compact-unique`, and those four capabilities gain a delta here for the first time.

## Goals / Non-Goals

**Goals**

- Make the solver's step a single record call, with no caller-visible phase before or after it.
- Remove the machinery that existed only to size buffers ahead of recording, so capacity changes stop being a two-function protocol.
- Stop the CPU from publishing parameters by writing GPU-visible memory.
- Make detector preparation the detector's own business.
- Make each GPU algorithm own its working storage, so a caller sizes and supplies only its own data (D12).

**Non-Goals (design-level)**

- No slot reuse, and therefore no change to how large the contact buffers get: capacity tracks the slot high-water mark and does not fall back when shapes are deleted. This is physics delete support and is future work. Grow-only fixes churn, not growth.
- No `DispatchIndirect` and no automatic barriers. Barrier placement stays manual; the dispatch surface inserts none.
- No change to the descriptor arena, the retirement queue, or the dispatch surface itself; those belong to `gpu-buffer-retirement`, `descriptor-arena-epoch-buckets` and `compute-kernel-dispatch`.
- No attempt to make the physics step frame-independent. The GPU-side serialisation across frames that physics relies on is provided by the frame submission's wait on the previous frame, and is unchanged here.

## Decisions

### D1: The phase dies because its justification died

`PreGPUStep` / `PostGPUStep` are removed from `ISolver` and `PhysicsSystem`, and their call sites are deleted. `PostGPUStep` costs nothing to remove — it has **no override anywhere in the engine**, and the only callers are the main loop, the editor, `PhysicsApp`, the editor-run-game example, and three headless tests that drive the phases by hand. `PreGPUStep`'s four jobs move to the record site, which D2 shows is possible.

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

The only CPU writes to GPU-visible memory in `engine/Physics` are `SetConstantU32` (`:260-263`), called twice for the hinge and fixed entry counts (`:506`, `:531`). Both counts are CPU-known: the host publishes `kJointSlotsPerJoint * joint_count` while the group's capacity is `kJointSlotsPerJoint * max(1, joint_count)`, so the count equals the capacity whenever a joint exists and is zero when none does. The **contact** group's count is produced on the GPU within the same substep; it is the only count a *clear or a reduction* reads from a binding. The joint groups' counts travel in the push block for both of those, and their entry passes also publish the same value into a device-local buffer for the radix sort's count guard — the one consumer that cannot read a push block (see below).

The shader side follows the convention the repository already applies in `copy_uint.comp` / `copy_uint_push.comp`: a CPU-known count travels in the push block, a GPU-produced count stays an SSBO, and the two cases are **separate shader files rather than a mode flag**. The counted entry-value clear therefore gains a push-count variant, and `SumByKey`'s entry-count input becomes a count source (`variant<uint32_t, const ComputeBuffer *>` in C++ terms) that selects its kernel.

**The radix sort is the one consumer that cannot take the count as a value.** `RadixSort::Record` derives a per-call pass count but reads the *data extent* from a bound `ElemCount` buffer at execution time, and `gpu-radix-sort` requires that buffer to be GPU-written by an upstream pass. The two joint groups therefore keep a count *binding* — but a device-local one, which the group's own entry pass publishes from the `joint_count` it already receives as a push value. The count is still CPU-known and still travels in a push block; it is simply materialized on the GPU because the sort cannot read a push block. So a joint group's **clear and reduction bind no count buffer** (they take the value), while its **sort** binds a GPU-written one, and no host-side write to GPU-visible memory remains anywhere in the step. The existing code, which CPU-wrote that buffer, was already in violation of `gpu-radix-sort`; this change fixes that as a side effect.

**The constraint that forces the split:** a kernel must never declare a descriptor binding it does not bind, because the dispatch surface rejects a missing binding (change C). A single shader that sometimes reads the count from a binding and sometimes from the push block would have to declare the binding unconditionally and leave it unbound on the CPU-known path. That is why this is two shaders and not one with a flag — the same reason `copy_uint.comp` and `copy_uint_push.comp` are two files.

**Alternative rejected:** keep the hinge and fixed counts in host-visible buffers and rotate them per epoch. It preserves the CPU write, which is the thing being removed, and the counts have no reason to be in memory at all.

**Alternative rejected:** make the contact count a push constant too, by reading it back. Any readback is a CPU/GPU stall inside recording, and the count is not known until an earlier pass in the same command buffer has run.

### D6: `clear_entry_values.comp`'s header comment is corrected, not deleted

The shader's header (`:12-14`) states that the entry count "is produced on the GPU in the same substep ... and cannot be a push constant". That is true of the contact group and false of the hinge and fixed groups. The comment is corrected on the buffer-count form to say which case it serves, and the push-count form carries its own header. This is recorded as a decision because the sentence reads as a general rule and would otherwise be copied into the new variant.

### D7: Detector configuration folds into recording, superseding `compute-kernel-dispatch`'s clause

`Configure` leaves both detectors' public surface. A detector prepares itself during `Record` — sizing its result buffers, acquiring its kernels, preparing its per-dispatch constants — when the geometry it observes has changed since the previous call, so the solver's step needs no configure step and the ordering assertion at `SpatialHashBroadDetector.cpp:674` disappears along with the state it guarded.

This **supersedes** `compute-kernel-dispatch`'s requirement that kernels be acquired in `Configure`, and its `physics-dummy-solver` text that keeps `PreGPUStep` as the initialization site. The reversal is deliberate: the property worth keeping from those clauses is *no pipeline is created in the middle of a call's dispatch sequence*, and that is preserved by requiring preparation to complete before the call's first dispatch. What is dropped is the claim that the acquisition site must be a different call, which existed to keep pipeline creation out of a lifecycle phase that no longer exists.

**Alternative rejected:** keep `Configure` as an internal method the solver calls at the top of `GPUStep`. It satisfies the letter of both changes but leaves two ways to prepare a detector, keeps the "did you configure it?" failure mode reachable from any future caller, and leaves `Record` unable to prepare itself when the geometry changes mid-step (which happens: the narrow detector's pair capacity tracks the broad detector's output, and both are recomputed per step).

**Residual staleness, recorded:** `xpbd-contact-solve`'s and `gpu-convex-collision-detection`'s scenario names still say "Detect" and "render graph" in places, because a `MODIFIED` block must reproduce existing scenario names verbatim. Their bodies are current; the titles are not, and renaming them is not possible from a delta. D12 adds three more of the same kind, in capabilities whose scenario names were written for the caller-owned arrangement: `gpu-radix-sort`'s *The result buffer is returned, not assumed* (recorded without a return value, and the note in the block says so), `gpu-sum-by-key`'s *Record sizing for the contact array* (about the level chain alone) and *The record layout is derived from the instance's own device* (titled for a caller-supplied record buffer that no longer exists), and `gpu-parallel-scan`'s *Construction rejects zero max_elem_count* (the parameter and the rejection are gone). The blocks state their own subject in the body, so a reader is not misled about the contract; only the titles carry history. `gpu-compact-unique`'s two mis-titled requirements are renamed instead, which is possible because a `RENAMED` block escapes the scenario check.

### D8: Count buffers no CPU code touches become device-local

`gpu_total_assignments` (`:182`), `gpu_global_count` (`:221`), `gpu_pair_count` (`:210`) and `gpu_unique_count` (`:242`) are created with CPU access but are never read or written by the CPU — verified by searching the detector and the solver for `GetVMAddress` and `Invalidate`, which find only `SetConstantU32`. All four are bound in hot kernels, where host-visible memory may cost bandwidth. They become device-local.

**Note:** this is not a synchronisation fix. These buffers were never a race, because the CPU never touched them. The change is about where the memory lives.

### D9: Storage reuse is bounded by a barrier, and the barrier between two calls is the caller's

Temporary storage is reused across calls and grows only when a call's geometry exceeds it (D3). Two recordings that share one storage set therefore have an execution-order dependency through it, and the barrier that dependency needs is the **caller's**, recorded explicitly, consistent with the pipeline's no-automatic-barriers rule.

Who owns the storage does not change that. D12 moves each algorithm's working storage into the algorithm, and a caller that records two calls on one instance still owes the barrier between them; what the move removes is the caller's ability to *avoid* sharing — the escape from the barrier becomes a second instance rather than a second buffer. This is stated once in `gpu-sum-by-key` and documented in the algorithm headers, because the failure mode is silent: a missing barrier produces a wrong sum with no error, and the reuse makes it reachable where a per-call allocation would not have.

**Alternative rejected:** allocate per call to make the barrier unnecessary. It reintroduces the churn D3 removes, and the retirement queue makes it *safe* but not free.

### D10: `GetResultBuffers()`' stability is preserved deliberately

The detectors return raw `ComputeBuffer*` and the solver caches the broad detector's pair buffers into the narrow detector. Retiring an allocation keeps the memory alive, but the `ComputeBuffer` facade is destroyed with the owning `unique_ptr`, so a pointer cached across a resize would dangle even though the memory would not. The pair buffers are therefore only re-pointed when their capacity actually changes, and the narrow detector's cached reference is refreshed from `GetResultBuffers()` at preparation time (D7) rather than held from a previous step.

### D11: The record-region partition is aligned to the device, not to the element size

`SumByKey::Record` partitions one caller-provided buffer into a key array and a value array per level, and binds each of those arrays as its own descriptor range. Both offsets were derived from record counts alone (`key_offset = cursor`, `val_offset = cursor + R_i * 4`, `cursor += R_i * (1 + num_channels) * 4`), which is a 4-byte alignment argument applied to a descriptor offset the device requires to be a multiple of `minStorageBufferOffsetAlignment` (16 on the development GPU). The layout is therefore invalid for ordinary geometries, and the validation layer reports it as `VUID-VkWriteDescriptorSet-descriptorType-00328` at the `RecValues` write. Three geometries in the tree hit it today: `capacity = 70000` with 3 channels (the headless test's randomized scenario; the second region's value array lands at byte 8792), the `capacity = 200000` / 7-channel example the `gpu-sum-by-key` scenarios use (byte 50104, from `R_2 * 4` after a 50048-byte first region), and any capacity whose level-0 block count `ceil(capacity / 256)` is odd, which puts the first region's value array at `2 * ceil(capacity / 256) * 4` — 488 for `capacity` in `[15361, 15616]`.

The fix is a padding change, not a binding change: a region's key array and value array are separate bindings and the shader addresses records through the `record_stride` push-constant field, so padding **between** the two arrays and **between** regions is invisible to the shader as long as a value array's channel stride stays exactly `R_i`. `Record` takes the alignment from its device context (`DeviceInterface::QueryLimit(PhysicalDeviceLimitInteger::StorageBufferOffsetAlignment)`), `GetRequiredRecordsBytes` gains the same alignment parameter and returns the padded partition, and a caller that sizes at one alignment and records at another gets a size error rather than a misaligned binding.

This is folded into this change rather than split out because both of its edit sites — `SumByKey::Record` and the `gpu-sum-by-key` requirements that describe the level chain — are already being rewritten here for the count source; a separate change would have to modify the same two requirements, which is exactly the cross-change layering the baseline convention above exists to avoid. It is also a prerequisite of this change's own verification: D4's sibling problem is that a value the CPU computes stops being usable as an address or a bound, and tasks 7.1 and 7.2 run under the validation layer.

**Alternative rejected:** align each region to a fixed 256 bytes, the largest value Vulkan permits for this limit. It keeps the sizing helper a pure function of `(capacity, num_channels)`, but it wastes up to 256 bytes per region and hardcodes a device property the RHI already exposes — `ParallelScan` documents the same precondition rather than assuming a value, and this repository queries device limits rather than guessing them.

**Alternative rejected:** bind each region as its own buffer. It removes the alignment question by removing the shared scratch, which D9 and task 2.5 exist to keep.

**Recorded, not fixed here:** `ParallelScan::Record`'s `block_sums_byte_offset` carries the same requirement as a documented `@pre` (`ParallelScan.h:117-119`) that no caller violates today — `RadixSort` passes `histogram_bytes = 1024 * num_blocks`, 256-aligned by construction. Enforcing it (a debug assert at the binding site) or converting it to an aligned layout belongs with that class, not here. For the same reason this change does **not** add an offset-alignment assert to `ComputeKernelResource` / `ComputeKernel::Dispatch`: the dispatch surface is an explicit non-goal of this change, and hardening it belongs to the change that owns it.

### D12: An algorithm owns its working storage; the caller owns its data

The four `gpu_algorithm` classes take working storage out of their interfaces. What moves in:

- `ParallelScan`'s block sums, one region per recursion level — and with them `max_elem_count` as a construction parameter, because the storage grows per call instead of being validated against a declared maximum.
- `RadixSort`'s transposed-histogram scratch and its ping-pong partner arrays.
- `SumByKey`'s record regions — and with them the `alignment` parameter of `GetRequiredRecordsBytes`, because the instance reads the alignment from its own device context.
- `CompactUnique`'s flag and offset arrays, plus the `ParallelScan` instance it used to be handed.

What stays with the caller is the data the caller owns and the algorithm cannot: the key and payload arrays a sort permutes and leaves its result in, the packed values, the output, the counts, the element-count buffers, and the buffers a detector or the solver sizes for its own passes. The rule is not "algorithms may allocate"; it is **an algorithm owns what no caller can observe**. `RadixSortOutput`, `RadixSortBuffers::keys_b`/`payload_b`/`scratch`, `GetRequiredScratchBytes`, `GetRequiredTempBytes`, `GetRequiredRecordsBytes`, `GetRequiredBlockSumsBytes`, `GetRequiredFlagBytes`, `GetMaxElemCount` and `max_elem_count` all leave the public surface with it.

Grow-only geometric capacity is unchanged and still lives in `rhi-buffer-capacity`: the algorithms call the same `Rhi::EnsureComputeBuffer` the detectors and the solver call, so the policy keeps one implementation and the algorithms contain no growth arithmetic of their own.

**Barrier consequence.** Two `Record` calls on one instance in the same command buffer alias the instance's storage and need a caller-recorded barrier between them. No call site changed: the solver already records a barrier between consecutive group sorts and between consecutive reductions, and the detector already records one between its two scans. `RadixSort` in fact already aliased its scan's block sums across passes inside a single call, so the pattern was in production; what changes is that the aliasing is no longer visible from the call site, which is why each class states the obligation in its own interface documentation.

**Why the copy-back.** A radix pass alternates which array it writes, so a call whose pass count is odd leaves its result in an array the instance owns. One extra linear dispatch copies it back into the caller's array on that parity, and it buys three things: the caller's array always holds the sorted records, so the interface needs no "which array" answer and `RadixSortOutput` disappears; the instance's partner array is then *pure* working storage, so one instance serves several data sets in one command buffer and the solver keeps its single sort instance for contact, hinge and fixed; and a host-side reader reads its own host-visible array rather than needing a staging copy. The cost is bounded: a 1-pass sort moves roughly `9N` bytes and the copy adds `2N`, and the copy-bearing 1-pass sort is still cheaper than a 2-pass one, so no capacity step makes a larger geometry cheaper than a smaller one.

**Alternative rejected:** keep caller-provided storage and delete only the plumbing that computes it. It leaves two owners for one decision and keeps the sizing formulas public without removing what they exist for.

**Alternative rejected:** have `Record` return a pointer into the instance's own array instead of copying back. The result's location would depend on the pass count, the pointer would stop being valid at the next call on the same instance — the dangling-facade hazard D10 exists to avoid — and every host-side reader would need a staging copy anyway.

**Alternative rejected:** allocate per call. D3's reasoning is unchanged: the retirement queue makes it safe, not free.

## Risks / Trade-offs

- **[Record-time reallocation is only safe because of a sibling change]** → Mitigation: this change depends on `gpu-buffer-retirement`; the ordering is stated in the proposal, and a build without the retirement queue would reintroduce the use-after-free the phase was avoiding. The verification tasks run under the validation layer with two to three frames in flight.
- **[Grow-only silently changes dispatch geometry]** → Mitigation: D4 fixes the two call sites that derive geometry from capacity in the same change, and the new capability states the rule; the search for `GetSize()` uses that feed a dispatch or an element bound is part of the verification.
- **[A count-source split doubles a shader]** → Mitigation: the split follows an existing, documented convention (`copy_uint.comp` / `copy_uint_push.comp`) and the shared body can be factored into a GLSL include, which the repository already uses for `common/` and `support.glsl`. The alternative — one shader with an unbound binding — is rejected by the dispatch contract.
- **[Removing `Configure` leaves detector preparation untested in isolation]** → Mitigation: the preparation path is exercised on every solver step, and the "no-op when nothing changed" case is a scenario with an observable assertion (allocation counters do not move between two identical steps).
- **[Deleting `PreGPUStep` removes the only place a solver could prepare outside recording]** → Mitigation: this is intended, and the record-time path is the only one that stays correct when geometry changes mid-step. If a future solver needs a genuinely expensive one-time setup, lazy acquisition on first use covers it without a phase.
- **[Reported size no longer equals element count]** → Mitigation: the rule is a named capability with its own scenarios, and D4 removes the two places in physics that relied on the old equality.
- **[Sibling changes' clauses are reversed here, so the archive order matters]** → Mitigation: the reversals are stated in the delta that performs them (D7, D12, and the removed requirements in `physics-push-constants`), and the cross-change baseline convention is stated above so a future reader can tell which text each block was based on.
- **[An algorithm's working storage is shared by every call on its instance]** → Mitigation: each class documents the caller's barrier obligation, and every current call site already records the barrier the data dependency needs between the same two calls, so the added aliasing is covered. A caller that wants two recordings that are independent for storage reasons uses two instances; the solver's three reduce groups serialise their sorts and reductions already.
- **[The odd-pass copy-back adds one linear dispatch]** → Mitigation: it lands only on odd pass counts, it is a pure linear pass (`2N` bytes against a `~9N`-byte one-pass sort), and the sort runs once per data set per substep rather than inside the iteration loops. The parity rule is pinned by the radix test, which reads the caller's array after a one-pass and a two-pass sort, so a missing or misplaced copy-back fails loudly.

## Migration Plan

1. **Fix the prerequisites that keep a computed value usable as an address or a bound (D4, D11).** Change the two helper dispatches to size from `elem_count` and document `GetCount()`; give the record-region layout its alignment padding, its alignment parameter on `GetRequiredRecordsBytes`, and its alignment source in `Record`. The dispatch-geometry fix is behaviour-preserving under exact-fit sizing; the alignment fix changes the record partition's size but not a reduction's result. Both must land before grow-only.
2. **Introduce grow-only geometric sizing (D3)** behind the shared sizing helper, and convert the five `EnsureBuffer` copies and the scene's SoA sizing to it. Verify with the existing tests before touching the lifecycle.
3. **Move the CPU-known counts to push constants (D5, D6):** add the push-count clear variant, give `SumByKey` its count source, extend the push blocks, and delete `SetConstantU32` and the two count buffers.
4. **Fold detector configuration into recording (D7)** and delete the `Configure` surface, together with the narrow detector's cached pair-buffer plumbing (D10).
5. **Collapse the lifecycle (D1):** move `PreGPUStep`'s remaining work into `GPUStep`, delete both phases from `ISolver`, `PhysicsSystem`, the main loop, the editor and `PhysicsApp`, and delete `Configure` call sites.
6. **Make the unused count buffers device-local (D8).**
7. **Move the algorithms' working storage into the algorithms (D12):** give `ParallelScan` its block sums, `RadixSort` its histogram and ping-pong partner arrays plus the odd-parity copy-back, `SumByKey` its record regions, and `CompactUnique` its flag/offset arrays and its own `ParallelScan`; delete the sizing helpers and the parameters that exist only to thread that storage through the call sites. Verify with the algorithm tests, then with the solver's fixtures.
8. **Verification:** the physics app's stability run and a long dynamic fixture, plus the full suite in both configurations.

Rollback: steps 1–2 and 6 are independently revertible (they change sizing and memory placement but not the lifecycle); step 7 is independently revertible (it moves ownership but not the step's dispatch sequence); steps 3–5 are the coupled core, and reverting them means restoring the phase, which is a mechanical revert of the same commits.

## Residual issues recorded

**One Release-only failure is accepted, not fixed.** `xpbd_reduce_capacity_test` — the fixture that resets two identically-shaped scenes and requires one further step to produce the same poses — fails in the release configuration and passes in Debug, on the same sources:

```
FAIL: body 1 differs after a shrunk contact set: 0.499751 vs 0.497956
FAIL: body 2 differs after a shrunk contact set: 1.4995 vs 1.49796
```

What the investigation established, so a later reader does not repeat it:

1. The pre-change tree passes in release, so grow-only geometric capacity is what exposes this. An exact-size bisect of `ComputeBuffer::EnsureCapacity` restores the pass.
2. Growing only the scene's buffers or only the detectors' buffers is harmless; the trigger is inside `XPBDGpuSolver::Impl::EnsureReduceGroup`, and it needs **both** the group's `values` buffer and its `out` buffer to be grow-only. Making either one exact again restores the pass, as does making both exact.
3. It is not a stale trailing read: clearing each `out` buffer over its whole capacity instead of the logical count does not change the outcome.
4. It is not a detectable out-of-bounds access: the core validation layer and GPU-assisted validation both report nothing, and both *mask* the failure by changing the allocation layout, which is the strongest evidence that the result depends on memory placement rather than on a logically wrong index.
5. The failure is deterministic (identical values across repeated runs), so it is not a floating-point race in the ordinary sense.

The most plausible remaining explanation is a layout-sensitive ordering or visibility defect in the reduce output path that the larger capacities make reachable — the same family of problem D4 fixes for dispatch geometry, in a place D4 did not reach. It is left open deliberately rather than papered over: the Debug suite (68/68, under the validation layer) and the release suite (67/68) are both recorded, and the next investigation should start from a per-element comparison of `values`, `out`, and the sorted key/payload arrays between the two fixtures rather than from another sizing bisect.

**The evidence above predates D12, and D12 moves the variable the investigation is about.** Every buffer whose sizing the bisect manipulated — the reduce groups' `values` and `out` excepted, which stay solver-owned — moves: the radix scratch and the record regions the groups used to own become the algorithms' own storage, the sort's ping-pong partner arrays become internal and the sorted result is copied back into the group's arrays, and the block-sums scratch the detector sized becomes the scans'. The allocation order, the buffer count and every capacity a buffer reaches therefore change again, so an exact-size bisect recorded before D12 no longer identifies the same buffers by name, and a reproduction has to be re-established from the tree as it now stands. The failure itself was not re-diagnosed here; this note only fixes which observations survived the move.

**What the post-D12 re-run adds, so the next reader starts further along.** The suite was measured again after D12, and the residual's shape is sharper:

1. **Every intermittent failure observed sits in the multi-level `ParallelScan` path** — a scan whose element count exceeds 512, which is where block sums, their recursive scan and the add-back exist at all. `gpu_sum_by_key_test`, which uses no scan, failed 0 times in 20 release runs; `gpu_compact_unique_test` (4096 scan elements) and `gpu_radix_multiblock_test` (768, 1024 and 5120) fail intermittently; no scan at or below 512 elements failed in any configuration.
2. **The trigger is neither the placeholder bindings nor the memory type.** Forcing every radix call to bind a real payload array — removing the keys-only path, where the scatter's payload bindings alias the key arrays — left the rate unchanged (4 failures in 25 release runs). Making every algorithm-internal working buffer host-visible instead of device-local left it unchanged as well (11 failures in 40 release runs).
3. **The radix test is the cheapest reproduction of the residual available:** `build/msvc/bin/Release/gpu_radix_multiblock_test.exe` runs in 0.4 s and fails roughly a quarter of the time, naming the first descending step. Over 25 release runs its three affected cases failed 7, 5 and 4 times. Debug failed 0 times in 10 runs under the validation layer, which is the masking already recorded above.
4. **Two further release-only failures belong to neither this change nor its siblings:** `descriptor_arena_test` (flaky, 4 of 5 runs) and `new_material_test` (deterministic fail-fast, 3 of 3 runs at 10 frames) fail the same way on a stashed tree at `db1e0237`, the commit this work started from. They are recorded here only so a later reader does not attribute them to the working-storage move.

## Open Questions

- **Whether `SumByKey`'s shared body should be factored into a GLSL include** or duplicated between the two count-source variants. **Resolved during implementation: factored.** The body, its bindings and its geometry live in `algorithm/sum_by_key_body.glsl`, and the two variants are thin shaders that declare their own push block and entry-count source. This is the option the risk note above preferred.
- **Whether the hinge and fixed reductions should be skipped entirely when their joint count is zero**, since the host knows the count is zero and the reduction is then a no-op over an unread group. Deferred: it is a dispatch-count saving with no contract change, and the current behaviour (entry count zero ⇒ no output, and a zero-count clear that is not recorded at all) is already correct.
- **Whether the retired `Configure` should leave a thin alias** for external callers outside this repository. Deferred: no caller in the tree needs one, and an alias would keep the ordering failure mode reachable.
