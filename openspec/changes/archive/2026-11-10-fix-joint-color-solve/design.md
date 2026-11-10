## Context

See `proposal.md` - Why. This section records only the facts that shape the approach.

The working tree already replaced the old "all constraints accumulate, then one merged apply" position iteration with a per-color Gauss-Seidel sweep for joints: `PhysicsScene::ColorJoints` greedily colors the joint descriptors, `XPBDGpuSolver::GPUStep` records, per color, a scratch clear, an accumulate pass, a `SumByKey` reduction and an apply pass. That rewrite fixed the real defect (a body's joint correction was divided by its total constraint count, contacts included) but multiplied the dispatch count:

| Scene | Bodies | Joints | Position-iteration dispatches | Measured step |
|---|---|---|---|---|
| 6-layer brick pyramid (no joints) | 22 | 0 | ~5 (contact only) | ~5.1 ms |
| A1 quadruped stand | 19 | 17 | ~25 (5 contact + 5 colors x 4) | ~15.5 ms |

Both run 2 substeps x 50 position iterations. The pyramid's step is dominated by its contact work, not by dispatch count, so the useful signal is the *difference*: ~2000 extra dispatches per step for 17 joints, which at the implied ~5-6 us per dispatch accounts for ~10 ms of the A1 step - twice the entire 200 Hz budget of 5 ms. The joint work itself is 17 joints of a few hundred flops.

Two facts constrain the solution:

- A joint's slot index is addressed from outside the solver. `PhysicsConstraintComponent` stores the slot it allocated so it can unregister the joint, and the scene's alive/lagrange arrays are keyed by that slot. Reordering the descriptor arrays would silently retarget those references.
- No existing physics shader orders storage-buffer writes within a workgroup. `barrier()` compiles to `OpControlBarrier Workgroup Workgroup 0x108`, i.e. `WorkgroupMemory | AcquireRelease` - it does **not** cover `UniformMemory`. Any intra-workgroup handoff of body poses must add `memoryBarrierBuffer()`.

## Goals / Non-Goals

**Goals:**

- A joint correction is applied once, at full strength, by the joint that produced it; no division by a contribution count and no per-body partial buffer.
- The joint path costs one dispatch per position iteration when the scene is small, and scales by total joint work - not by `colors x joint_count` - when it is large.
- Joint slot ids and the public `JointId`/actuator/readback surface are unchanged.
- The joint entry-list, joint reduction, joint partial-buffer and push-counted-clear machinery is deleted rather than left unused.

**Non-Goals:**

- Changing contact solving. Contacts keep their Jacobi accumulation, segmented reduction and `1/count` averaging; the averaging is intentional there and is the reason a contact manifold behaves like a soft, many-row constraint.
- Contact dispatch scaling (the contact capacity of 50k points makes the contact pass capacity-bound at large body counts). That is the next bottleneck but not this change.
- Retuning compliance, iteration counts, or the actuator gains.
- Multi-island work assignment, indirect dispatch, or a device-side global barrier.

## Decisions

### D1. A color's joints write body poses directly; no scatter, reduction or apply for joints

Because the coloring invariant is "two joints of a color share no body", a color pass has exactly one writer per body. The reduction that existed to merge multiple writers is therefore a sum of one element, and the apply pass that divided by the count is what produced the dilution bug. Both are deleted for joints: the accumulate shader reads its two bodies, evaluates both constraints, and adds the correction to the position and orientation buffers.

This also restores momentum conservation. The old apply divided each body's partial by that body's own count, so the two ends of one joint were scaled by different factors and the pair of corrections no longer summed to zero. Under direct writing each body receives the full, equal-and-opposite correction.

Alternatives considered:

- *Keep scatter/reduce with a count of one.* Bit-identical result, no dispatch saved; the reduction and its buffers stay for no reason.
- *Keep the merged apply and drop only the division.* Restores full-strength Jacobi instead of Gauss-Seidel; over-relaxes bodies with several incident constraints and oscillates.

### D2. Joint descriptors keep their order; colors are published as a bucket index list

`PhysicsScene` publishes, per joint type, a `sorted_ids` array (joint slot ids ordered by color) and a per-color range table. A dispatch reads `sorted_ids[offset + i]` and then the descriptor by slot id. Descriptor arrays, `alive` arrays and lagrange arrays are never permuted.

Reordering in place was rejected: `PhysicsConstraintComponent` holds the slot index it allocated, so an in-place sort would make a later `UnregisterHingeJoint` disable a different joint. The same hazard is why the bucket list, not a permutation of the descriptors, is the only indirection.

Within a color the list SHALL preserve slot order (a stable sort). In particular it SHALL NOT be sub-sorted by connected component: a per-island secondary order would make every warp hold only one robot's joints, which is the exact utilisation loss this change exists to avoid at large body counts.

Alternatives considered:

- *Per-lane color compare (`if (joint.color != current_color) return;`) with the fixed `ceil(joint_count / workgroup_size)` dispatch.* This is what the working tree does. It costs `colors x joint_count` lane evaluations instead of `joint_count`, and it reads every descriptor once per color. At 17 joints the workgroup count is one either way, so it is pure waste; at ~17k joints (1024 quadrupeds) the hinge path would dispatch 4x the needed workgroups.
- *Sort the descriptors in place.* Rejected for slot stability (above).

### D3. One dispatch per non-empty color, bounded by that color's joint count

`workgroups = ceil(n_c / workgroup_size)`, and colors are separated by the solver's existing `DispatchBarrier`. The dispatch geometry follows the color, so an idle lane only exists in a color's last partial workgroup.

A single dispatch over all colors (for example a 2D grid whose Y index selects the color) was rejected: colors must be *sequential* because two different colors can share a body, and a compute dispatch cannot order its own workgroups without a device-side barrier, which Vulkan does not guarantee.

Alternative considered:

- *One persistent workgroup per connected component, looping the component's colors internally.* Islands never share a body, so this is correct without global barriers, and it is what the single-workgroup mode (D4) degenerates to. Rejected as the general scheme because at 1024 independent robots each workgroup would hold 17 joints, i.e. ~5 active lanes of 32, whereas the global color buckets pack joints from many robots into full warps (~100% utilisation). It also needs island tracking on the CPU.

### D4. Single-workgroup sweep as a gated fast path

When every color of a joint type fits in one workgroup (`max_color_count <= workgroup_size`), the whole sweep is recorded as one dispatch whose only workgroup walks the colors in order. Between colors it executes `memoryBarrierBuffer(); barrier();`, which orders one invocation's buffer writes before another invocation's reads within the same workgroup.

The shader must not `return` before the last barrier, and must not skip a barrier under any input: the joint work is wrapped in a guarded block inside the loop, never an early exit. This is the one deadlock-shaped hazard in the change.

Why it is worth a second mode: for a single robot the per-color path is C dispatches of one workgroup each, i.e. C dispatch latencies to move 3-5 joints; the sweep pays one dispatch latency plus C cheap in-shader barriers.

Alternatives considered:

- *Body poses in `shared` memory instead of SSBO + `memoryBarrierBuffer()`.* Avoids relying on buffer memory semantics, but bounds the mode by shared-memory capacity and needs a load-in/store-out of every body. The buffer barrier is cheaper and unbounded.
- *A device-side global barrier so one dispatch can sweep colors with many workgroups.* Rejected: Vulkan provides no forward-progress guarantee for a spin-wait barrier; it is not a portable implementation.

### D5. The two joint types share one constraint-math include

The per-joint hinge and fixed evaluation moves into a shared GLSL include with one entry point per type. Three shaders consume it: `accumulate_hinge_position.comp` and `accumulate_fixed_position.comp` (one color each) and `joint_color_sweep.comp` (all colors of both types in one workgroup). Keeping the math in one place is what makes "the two modes produce identical results" checkable.

The fixed colors run before the hinge colors in both modes, preserving the working tree's order. Hinge and fixed are *not* merged into one color graph: a quadruped trunk already has four hinge edges plus one fixed edge, so the merged graph's maximum degree is 5 and its color count is the same as `4 hinge colors + 1 fixed color`. Merging would only add descriptor churn.

### D6. Coloring: greedy first-fit, alive joints only, kinematic bodies never a conflict

Colors are assigned by deterministic greedy first-fit over the joint slot order, independently per joint type. Two joints conflict when they share a body index **that the solver can write**, so:

- a joint with `alive == 0` consumes no color and appears in no bucket;
- a body marked kinematic never creates a conflict, because the solver writes no kinematic body. A quadruped whose base is bolted to the world therefore drops from its trunk degree (four hinge colors) to the alternation its leg chains allow (two), because the four hip joints no longer compete for the trunk.

Degree-descending ordering (Welsh-Powell) is *not* adopted now. It can even out a star's color sizes - the A1's 5/5/1/1 is a consequence of the greedy slot order, not of the topology - but it cannot reduce the color count below the maximum body degree, and color size only matters when a color crosses a workgroup boundary, which the gated sweep (D4) already covers for small scenes. It remains a pure CPU-side improvement if measurement ever shows a production scene whose color sizes straddle the workgroup size badly.

### D7. Rebuild the color data only when the joint graph changes

`Scene::FlushPhysics` runs every frame, so a naive recolor-and-rebucket would sort every joint every frame. The scene SHALL rebuild only when a joint is allocated, unregistered, submitted with new body indices, or a body's kinematic flag changes, and SHALL reuse its scratch arrays. The rebuild produces the bucket list and the color counts that `PhysicsGpuBuffers` exposes; the solver consumes them without recomputing anything.

### D8. Delete the joint entry-list machinery, and the descriptor color field

With joints no longer reduced, nothing needs a sorted `(body, slot)` list for them. This change removes `entries/hinge_entries.comp`, `entries/fixed_entries.comp`, `clear_entry_values_push.comp`, the hinge/fixed `keys`/`payload`/`values`/`out` buffers, the joint entry counts, and the joint halves of the push-counted clear and reduction paths. The descriptor's temporary `uint32_t color` field reverts to `_pad`: the shader never needs a per-joint color, only the bucket ordering.

This is a larger diff than the working tree's, but leaving the machinery in place would mean paying two dead dispatches and two radix sorts per substep, and keeping a GPU data field that no shader reads.

## Risks / Trade-offs

- **[Behavior change beyond the working tree]** The dilution fix changes joint stiffness for any body that also carries contacts, which is exactly what the A1 stand test exercises. → Gate on the existing `physics_app_actuator_stand_test` (upright, settle speed, joint error) and re-check the actuator gains only if the test regresses.
- **[Two dispatch modes drift apart]** The sweep and the per-color path could compute different corrections. → One shared constraint-math include, plus a test that runs the same scene through both modes and compares poses.
- **[Sweep deadlock or stale read if a shader exits early]** → The spec forbids it and the shader keeps the joint body in a guarded block; no `return` inside the color loop.
- **[Sweep mode bounded by workgroup size]** Scenes with a large color silently use the per-color path; the mode choice is CPU-side and observable, not a correctness issue.
- **[Larger CPU-side bookkeeping]** The bucket build adds `O(N log N)` work at a topology change instead of `O(N)` per frame. → Dirty gating (D7) keeps it off the hot path; the common case is a topology change at scene load.
- **[The next bottleneck is already visible]** At large body counts the contact pass is capacity-bound (`max_contact_points = 50000` drives the accumulate dispatch, not the live collision count), so this change will not make 1024 quadrupeds fast on its own. → Out of scope; recorded here so the measurement is not misread.

## Migration Plan

1. **Direct write per color.** Keep the descriptor `color` field and the per-lane compare for one revision; replace clear/accumulate/reduce/apply with a single direct-write accumulate. This isolates the correctness change (no more averaging) from the scheduling change and should be bit-comparable to the working tree apart from the removed division.
2. **Buckets and deletion.** Publish the bucket list; switch the accumulate shaders to read `sorted_ids[offset + i]`; delete the joint entry/reduce machinery; revert `color` to `_pad`.
3. **Sweep fast path.** Add `joint_color_sweep.comp`, gate it on `max_color_count <= workgroup_size`, and measure the threshold (64 vs 128 workgroup size) on the A1 stand and a multi-robot scene.

Rollback is per phase: each phase leaves the solver in a coherent state, and the stand/pyramid tests run at each step.

## Open Questions

- Workgroup size and the sweep threshold (64 vs 128): both are configuration, measurable after phase 3 without touching the specs.
- Whether the fixed and hinge streams should eventually merge into one color graph: no benefit at the current maximum degrees, revisited only if a production robot's joint layout changes that.
- Whether the contact path's capacity-bound dispatch should be addressed next (indirect dispatch from the live collision count): a separate change.
