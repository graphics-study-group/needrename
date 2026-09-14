# Design: Entry-Count-Driven Reduce

## Context

See `proposal.md` — Why for the motivation (per-iteration work scaling with the worst-case contact capacity) and the estimated cost. This document covers how.

Current state of the path being changed:

- `SumByKey` and `RadixSort` both take their element geometry as **constructor** parameters and cannot be resized in place. The solver compensates with `Impl::EnsureSortAndSum`, which asks the objects whether their stored geometry still matches the current counts and destroys/recreates them when it does not. That mechanism is the origin of the stale-capacity bug fixed at the end of `remove-float-atomics`.
- The radix passes **already** guard on a GPU-side count (`pair_count.count`, read at execution time) and were designed for a count produced on the GPU. The solver is the outlier: `PreGPUStep` writes `contact_cap` into that buffer from the host, which disables the guard and makes the sort process the full capacity.
- Three reduce groups exist (contact / hinge / fixed), each with its own pair ping-pong, count, value scratch, record buffer and output. The radix passes already read only the first `pair_count.count` elements, but `PreGPUStep` writes `contact_cap` into the contact group's count buffer, so that guard is currently a no-op for contacts. Hinge and fixed capacities are already tight (`4 * max(1, joint_count)`, CPU-known), and their counts must become `4 * joint_count` so that the reduction's read bound is the only thing the shader relies on (D4); only the contact group is badly over-allocated, because the narrow phase produces its count on the GPU.
- `max_contact_point = max(1, min(all_pairs * 5, config.max_contact_points))` is a **true worst-case bound**, not padding: `detect_collisions.comp` appends up to 5 manifold points per candidate pair via `atomicAdd(collision_count.v[0], num_points)`. It must not be tightened.
- The unarchived `remove-float-atomics` change owns the `gpu-sum-by-key` capability and two of the `xpbd-contact-solve` requirements this change modifies. This change's deltas are written against the **post-archive** state of those specs and must be archived after it.

## Goals / Non-Goals

**Goals:**

- Make every per-iteration operation's data extent follow the real entry count: the scratch clear and the level-0 reduction read. The dispatch *count* stays capacity-derived everywhere (the CPU cannot know the real count, and the RHI has no dispatch-indirect). The radix sort is left alone: its element reads are already count-guarded, and only its dispatch geometry is capacity-derived.
- Remove construction-time geometry from both GPU algorithm classes so a geometry change is an ordinary per-call parameter rather than a destroy-and-rebuild.
- Keep observable physics identical. This change alters *how much* is read, not *what* is computed.

**Non-Goals:**

- Reducing the per-iteration barrier count or batching the three constraint types' dispatches. Analysed and deliberately deferred: the three accumulates already write disjoint buffers and need no barrier between them, so there is a real ~2x barrier win available (measured from the recording code: 10 explicit barriers plus `k-1` internal ones per position iteration, ~860 per frame), but it is a separate structural change.
- Merging the three constraint types into one reduce group.
- Relaxing Scheme A (the entry passes keep writing every slot).
- Making the level-0 reduction read linear rather than gathered; see Decision D10.
- The descriptor-set cache growth, the binding offsets, and the buffer allocation strategy; these are tracked as a separate issue (Decision D8).

## Decisions

### D1: Geometry is a call parameter; the algorithm classes hold no geometry

**Choice**: `SumByKey(DeviceContext&)` and `RadixSort(DeviceContext&)`. `Record` receives the geometry it needs, and the record-region partition is derived inside the call.

**Why it is possible at all**: nothing the shader reads is baked. `input_count`, `num_channels`, `max_key_value`, `record_stride` and `gather_pairs` already travel as push constants; the record-region offsets and sizes are computed *inside* `Record`'s level loop, at binding time, not at construction; and `GetNumLevels` / `GetRequiredRecordsBytes` are already `static`. Every stored field is therefore a pure function of the constructor arguments, and the constructor's only remaining jobs were validation and convenience.

**Consequence**: `EnsureSortAndSum` and the whole rebuild path are deleted, one instance serves every group and every geometry, and the stale-capacity bug class becomes unrepresentable rather than fixed.

**Alternatives considered**: keep the cached geometry and rebuild (status quo — it is the bug source); keep `max_elem_count` purely as a bound (it is the field's only remaining use, and the same protection is available per call — see D6).

### D2: Two lengths bound a level: the level element count and the entry count

A level's input array is bounded by two different numbers, and conflating them is the trap this change exists to avoid.

- The **level element count** is the length of the array the level reads: `R_0 = capacity` at level 0, `R_i` at level `i >= 1`. It is a pure function of the capacity, so the CPU computes the whole chain (`ComputeGeometry`). It alone determines the element-index bound, the per-block real extent that run classification uses, the record-region partition `R_{i+1} = 2 * ceil(R_i / 256)`, the per-level workgroup count, and the level-0 channel stride of the value buffer. The shader keeps reading it as `input_count`, and it stays the capacity at level 0.
- The **entry count** is how many leading elements carry real data. It bounds data extent only: no value is read for an element at or beyond it. It never touches geometry.

`SumByKey`'s level 0 today has `input_count == capacity == count`, which is why one field sufficed. This change cannot make `input_count` mean the count: the workgroup count must stay `ceil(capacity / 256)` (D3), the per-element index mapping `e = wg * 256 + tid` must stay consistent with the array it indexes, and `values[c * capacity + slot]` must keep its capacity stride. Making `input_count` the count would break the index mapping, the run classification's real extent, the level-0 stride, and the "is this the last level" test all at once.

**Consequence**: the entry count travels separately, and because it is produced on the GPU it travels in a **binding**, not in the push-constant block: `Record` binds the caller's entry-count buffer and the shader reads it at execution time under the same guard-manner as the radix sort's pair count. The push-constant block keeps its five fields and does not grow.

**Where the count is read**: only in gather mode, which only level 0 uses. A deeper level's input is the record region the previous level just wrote, so it is entirely valid and there is nothing to bound — the shader's non-gather path applies no count bound, and the `entry_count` binding is bound at every level (unread where `gather_pairs` is zero) only so the descriptor set stays complete, exactly as `PairsIn` already is.

**`value_stride` is not needed.** An earlier revision of this design split the level-0 channel stride into its own push field. With the level element count staying at the capacity, the stride is already `input_count` at every level (`capacity` at level 0, `R_i` above), so the existing `values_in.v[c * input_count + ...]` expression is correct unchanged.

### D3: Level 0 dispatch stays capacity-derived (the trap in this change)

**Choice**: level 0 always dispatches `ceil(capacity / 256)` workgroups and every level reads its input as an array of `input_count` elements; only the value stores are bounded by the entry count.

**Why**: the number of workgroups determines how much of record region 1 is rewritten. `k` and the region sizes are capacity-derived, so region 1 always has `R_1 = 2 * ceil(capacity / 256)` slots and level 1 always reads all of them. If level 0 dispatched by the entry count:

- a count below the capacity leaves record slots from an earlier call in place. Those records may carry valid keys, so level 1 sums partials that belong to a previous substep — phantom contributions, silently.
- a count of zero dispatches zero workgroups, so the region is not rewritten at all.

The same reasoning forbids making the entry count the *array* bound: the shader locates each element as `wg * 256 + tid` and classifies a block's runs against its real extent within `input_count`. If `input_count` became the count, every block past the first would consider its shared-memory contents (all out-of-range padding keys) to be the array, and the blocks that hold real data would misclassify their last run as complete — which silently drops one partial sum per key whose run crosses the truncation point.

**What makes the count safe to apply to values only**: the pair array is ordered by key, and the slots at or beyond the count are exactly the slots the entry passes wrote `INVALID` for, so the entire tail of the array is one inert run whose keys are never below `max_key_value`. Zeroing the tail's values therefore removes exactly the entries the caller declared absent, and leaves the record chain's structure (which block emits which boundary record) completely untouched.

**No companion fix is needed.** An earlier revision of this design added "an empty block must write both of its record slots". With the level element count staying at the capacity, `real_count = min(256, capacity - min(base, capacity))` and `base <= capacity - 1` because the workgroup count is `ceil(capacity / 256)`, so `real_count >= 1` at every block of every level: empty blocks do not exist and the record chain is dense by construction.

**Alternative considered**: size level 0's dispatch by the count and zero-fill region 1 separately — an extra pass to avoid a case that staying capacity-derived avoids for free.

### D4: Count ownership — GPU for contacts, host for joints, one read path

The count cannot be a push constant: it is produced on the GPU and must be read at execution time. It cannot be `collision_count` used directly either, because the reduction needs **slots**, and each contact point owns two (`slot = cidx * 2 + side`); reusing the detector's buffer would also put the solver's slot rule inside another component's contract.

**Choice**: each group's count is written by whoever knows the number — the contact entry pass derives `min(2 * collision_count, entry_capacity)` on the GPU, and the host writes the hinge/fixed counts. `SumByKey` reads the count from a bound buffer for the contact group and in gather mode only, so it needs no per-type branch beyond the `gather_pairs` flag it already has, and the host-written joint counts need no special path.

**Joint counts are the exact joint slot count**: `4 * joint_count`, not the group's capacity `4 * max(1, joint_count)`. The two agree whenever a joint exists, so the difference is exactly the `joint_count == 0` case, where the capacity is clamped to one joint's worth so the entry pass has somewhere to write. Publishing `4 * joint_count = 0` there means "ignore every slot of that group", which is also what keeps the read bound the only invariant: because the capacity keeps its `max(1, ...)` form, publishing the capacity instead would tell the reduction to read a slot group that belongs to no joint, and the shader would then rely on those slots happening to hold `INVALID`. It happens to be true today (the entry pass writes the owner only when `j < joint_count`, and with zero joints `j == 0` fails that test), but relying on it would make the reduction's correctness depend on an unrelated component's sentinel choice. The alternative — gating those slots in the accumulate shader by re-reading `joint_count` — would add a joint-state read to a shader the entry-pass contract deliberately keeps state-free.

**Note**: this does not add a buffer. `gpu_contact_count` already exists; only its writer changes.

**Alternative considered**: pass the capacity as the count (the "fallback"). Rejected — it is exactly today's behaviour, and it is *not* equivalent to a guarded dispatch. Neither the radix passes nor `SumByKey` have an "invalid key, return early" path: the radix guard is `idx >= pair_count.count` and is evaluated before any load, and `SumByKey` only uses key validity to suppress the output *write*, not the load or the reduction. Passing the capacity therefore removes the only early-out rather than adding one.

### D5: Counted clear as a separate shader

**Choice**: a new `clear_entry_values.comp` that reads the entry count from a buffer and clears `num_channels` channel planes with the capacity as the stride. `clear_int_buffer.comp` keeps its current flat, push-constant-length job for the per-body and lagrange buffers.

**Why not extend `clear_int_buffer.comp`**: it has no count buffer bound, and giving it one would require either every existing call site to bind a dummy or a mode flag — the latter is the pattern this codebase rejected for the accumulate shaders (`remove-float-atomics` design Decision 11). A second shader matches the existing precedent (`memset_uint.comp` already exists alongside `clear_int_buffer.comp`).

**Why the clear can be bounded at all**: the reduction reads values only at the payload slots of entries below the count, which is a subset of `[0, count)`. Slots at or above the count are never read, so they never need clearing.

**Dispatch geometry versus data extent**: the count is produced on the GPU in the same substep, so the workgroup count for this shader is `ceil(kNumChannels * capacity / 64)` — exactly the workgroup count the flat clear already uses — and the shader early-outs once the slot index reaches the count. The dispatch is therefore unchanged and only the writes shrink. A flat early-out like `idx >= kNumChannels * count` would be wrong: the cleared elements are not the first `kNumChannels * count` flat indices, they are the first `count` slots of **each** of the `num_channels` planes, so the shader has to decompose the flat index into `(channel, slot)` before comparing. Any scheme that made the workgroup count follow the count would need indirect dispatch, which is out of scope.

**Rejected**: having the accumulate write zero to its own slots (`zero_slot`). It removes the clear and the barrier around it, but forces every accumulate shader to carry contributed/did-not-contribute branching and diverges them from their pre-change text — the property `remove-float-atomics` was built to preserve.

### D6: Validation moves per call

Removing the stored geometry removes the construction-time bound it enforced. Both `Record` implementations keep an equivalent guard by checking the per-call capacity against the buffers bound for that call (`ComputeBuffer::GetSize()`), so a wrong capacity is still an error rather than an out-of-bounds write. `SumByKey::Record` also validates `num_channels`, a zero capacity and a zero `max_key_value`, which the constructor used to do.

### D7: Keep Scheme A and the `max(1, ...)` clamp

The entry passes continue to write every slot in `[0, capacity)`, and hinge/fixed capacities keep their `max(1, ...)` floor. Both are now convenience/robustness rather than load-bearing: with a dynamic count the tail's *values* are unread, so keeping the write costs ~1.6 MB per substep and saves revising a contract plus several scenarios, and the clamp keeps `capacity == 0` (the `ComputeGeometry(0)`, zero-byte record buffer, zero-workgroup dispatch path) out of scope.

Two consequences worth stating because they are not obvious:

- **The count no longer follows the capacity for the joint groups.** The host publishes `4 * joint_count` (D4), so with zero joints the hinge/fixed counts are zero while their capacities are 4. The full-capacity write is what keeps the ignored slots well-defined (`INVALID` keys, which sort last), and the exact count is what keeps the reduction from reading them.
- **The joint groups gain nothing from this change.** Their count equals their capacity whenever a joint exists, so both the counted clear and the count-bounded reduce are identity operations on them. All of the benefit is in the contact group, whose capacity is a worst-case bound (`2 * min(all_pairs * 5, max_contact_points)`) far above the live contact count.

### D8: The descriptor-set cache is out of scope, with one interaction recorded

Binding `(buffer, offset, size)` combinations are cached in a map that is never pruned, so any change in those triples mints a descriptor set that is never released; buffers are also reallocated whenever the capacity changes. Both are being tracked as a separate issue — a planned redesign of the compute shader and binding layer — and are not touched here.

**Interaction to record**: this change has two opposite effects on that cache. Deleting the instance rebuild means a geometry change no longer destroys the object that owns a `ShaderResourceBinding`, so the churn stops being scattered across discarded objects and no longer re-mints a warm cache per rebuild. Against that, one shared instance with per-call binding means the distinct `(buffer, offset, size)` triples of a single frame all land in one cache, and the entry-count binding adds one more per level. This change claims no correctness or performance property that depends on that issue; the note exists so the churn is not silently attributed to this change either way.

### D9: Rename before changing behaviour

The current names mislead: `pairs_a` / `pairs_b` read as two live arrays when `b` is only the sort's ping-pong temporary; the "scratch" holds the real contribution values while "records" is purely internal to the reduction. The rename lands as its own commit so the behavioural diffs stay readable.

| Current | New |
|---|---|
| `gpu_*_pairs_a` | `gpu_*_entries` |
| `gpu_*_pairs_b` | `gpu_*_entries_tmp` |
| `gpu_*_scratch` | `gpu_*_values` |
| `gpu_*_records` | `gpu_*_reduce_scratch` |
| `gpu_radix_scratch` | `gpu_radix_histogram` |
| `gpu_*_count` | `gpu_*_entry_count` |
| shader binding `ScratchValues` | `Values` |

### D10: Decisions carried in from the preceding analysis (recorded so they are not re-litigated)

- **The `slot` payload stays.** The sorted record is a `uvec2` (8 bytes). Carrying the 7 value channels through the sort instead would move 28 bytes per record through a structure touched 8 times (4 passes x read+write) versus once in the reduction — an estimated 2.7x–3.5x more traffic, to save a single gathered read. `slot` is the argsort payload that keeps the sort cheap.
- **The gather stays on the read side.** A permutation must be carried by one side or the other. Writing at the sorted position requires inverting the permutation into a `pos_of` map (a pass, two capacity-sized buffers per type, and a bijection invariant); reading by slot requires one indirection at level 0 and makes the write an identity index (coalesced for contacts). The read side was chosen.
- **`max_contact_point`'s `*5` factor is not padding** and must not be tightened.
- **The three reduce groups stay separate.** Merging them would let a single sort and reduction serve the position phase and remove two thirds of the reduce dispatches, but the velocity phase only has contact contributions, so it would either over-read the joint slots or need a second entry list. Deferred.
- **Barrier batching is deferred** (see Non-Goals).

## Risks / Trade-offs

- *[The entry count is mistaken for a geometry input]* → This is the change's central trap and it is documented in three places: D2/D3, the `Record` requirement's "Two different lengths" clause, and a dedicated scenario asserting that level 0 dispatches `ceil(capacity / 256)` regardless of the entry count. Concretely, letting the count reach the element-index bound breaks the index mapping, the per-block real extent used for run classification, the level-0 channel stride, and the last-level test at once.
- *[Count and capacity disagree, so the reduction reads a range the entry pass did not write]* → The count is produced by the same pass that writes the entries (contacts) or by the host from the same number that sizes the capacity (joints), and both are clamped to the capacity. The reduction treats entries at or beyond the count as absent, and a clear never leaves a read below the count uncovered.
- *[A slot at or beyond the count carries a key that can contribute]* → For contacts the entry pass writes `INVALID` for every slot whose contact does not exist, so the tail sorts last and is inert. For joints the host publishes the exact joint slot count, so the ignored slots are the ones the entry pass could not mark `INVALID` (the `joint_count == 0` case where the capacity is clamped to one joint's worth). Both are asserted by scenarios.
- *[Removing the construction-time bound loses an out-of-bounds guard]* → Replaced by per-call checks against the bound buffers' sizes (D6).
- *[Scheme A's full-capacity entry write remains as residual cost]* → ~1.6 MB per substep, once per substep rather than per iteration. Deliberately kept (D7); it does not shrink with the count.
- *[The radix sort's dispatch is still capacity-derived]* → Its histogram and scatter passes already guard on `pair_count.count` before any load, so this change leaves their behaviour correct without touching them. What remains is `4 passes x 3 sub-dispatches x ceil(capacity / 64)` workgroups, of which all but the counted prefix return immediately; that is a dispatch-scheduling cost, not the memory traffic the proposal's numbers describe, and shrinking it needs indirect dispatch (out of scope). Recorded here so the two are not confused when the verification task measures the result.
- *[The joint groups are unaffected]* → Their count equals their capacity wherever a joint exists, so the counted clear and the count-bounded reduce are identity operations on them; the whole benefit comes from the contact group. Recorded in D7 so a measurement that only exercises joints is not mistaken for a failure of the change.
- *[Performance claims are estimates, not measurements]* → The verification tasks measure the change on the physics application and record before/after numbers; no CI assertion depends on a specific speedup, because the CI environment cannot produce a stable one.
- *[This change's deltas reference requirements that exist only inside the unarchived `remove-float-atomics`]* → Archive order is documented in the proposal. `openspec validate` does not enforce the ordering, so the archive step must respect it.
- *[Spec drift carried along]* → The main `gpu-radix-sort` spec still describes a `RenderSystem&` / `AddPasses` / `RenderGraphBuilder` API that the code no longer has. The MODIFIED block for `RadixSort class construction` has to describe the constructor being changed, so it is written against the real API. The remaining drift (the `Single-call AddPasses API` requirement) is left alone and noted here rather than silently rewritten.
- *[A scenario name outlives its premise]* → `gpu-radix-sort`'s `Construction rejects zero max_elem_count` cannot be dropped (a MODIFIED block replaces the whole requirement and archive refuses to lose scenarios) and its premise is gone. It is kept under its original name with content describing the replacement behaviour.
- *[A pre-existing spec/code mismatch carried along]* → `gpu-sum-by-key`'s `Single level for a small array` scenario asserted that no record-buffer binding is written when the capacity is at or below 256. The shader does not satisfy that with the current code: `SumByKey::Record` binds `RecKeys`/`RecValues` to the output buffer for `k == 1`, and a block whose first run starts at index 0 takes the `touches_first` path and stores its partial into that alias before the last-element check. The write is harmless (the value is overwritten by the next branch's output store, and the buffer is bound to a scratch-sized range) but the assertion was false. This change rewrites the scenario's second bullet to state the property that actually matters — a single block finalises every run, so nothing is carried to another level — instead of asserting an implementation detail that was never true.
- *[The reduction algorithm itself is untouched]* → This change alters which elements are read, never how a block reduces them. Switching the block reduction from the current run-local linear sum to a segmented log-depth reduction is a separate piece of work with its own measurement; it is deliberately not folded in, so a wrong sum cannot be attributed to a semantics change.

## Migration Plan

Stepwise commits, each independently buildable:

1. **Rename only** (D9). No behaviour change; verified by a full build and the existing suite.
2. **Shaders**: `sum_by_key.comp` gains the entry-count binding and bounds value reads by it in gather mode; add `clear_entry_values.comp`. Both compile; nothing calls them yet.
3. **Algorithm API**: `SumByKey` and `RadixSort` become geometry-free, `Record` takes the geometry and the entry-count buffer, validation moves per call (D1, D2, D6). The headless algorithm tests are updated in the same commit so the tree stays green.
4. **Solver wiring**: one `RadixSort` and one `SumByKey` instance, `EnsureSortAndSum` deleted, per-call geometry, the contact entry pass publishes its count, hinge/fixed publish `4 * joint_count`, the counted clears replace the full-capacity scratch clears (D3, D4, D5, D7).
5. **Coverage**: the count-below-capacity, zero-count and garbage-tail cases, plus the capacity-change harness carried over from `remove-float-atomics` task 8.5.
6. **Verification**: full build, full suite, `openspec validate`, and a before/after measurement on the physics application recorded in the change's tasks.

Rollback is a revert; nothing here changes a data format, a serialization path, or a public API, and every affected buffer, shader and algorithm entry point is solver-internal.

## Open Questions

- Where the capacity-change harness should live. `PhysicsApp` freezes the scene at `CommitScene` (`Add*` throws afterwards) and there is no headless `PhysicsScene` + `XpbdGpuSolver` fixture under `test/`, so the harness has to be built. The shape of that fixture is a test-infrastructure question and does not affect the specs, the approach, or the task breakdown beyond the one coverage task. It is also the largest unknown in the plan: tasks 5.1-5.3 all depend on it, so if it turns out to be expensive it should be split into its own change rather than block commits 1-4.
- Whether the per-call capacity checks throw or assert. The tasks assume they throw (matching the guard they replace); if the project prefers asserts for internal invariants, only that task changes.
- Whether the contact entry pass should keep writing `INVALID` for the whole capacity when the collision count is zero. The write is required for the record chain to stay dense, but the `INVALID` value itself is not load-bearing for the tail any more (the count is), so the long zero-contact run is pure cost. Not worth a branch in the shader today; recorded so it is not mistaken for a correctness requirement later.
