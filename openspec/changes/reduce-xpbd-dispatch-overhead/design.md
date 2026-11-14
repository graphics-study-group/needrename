## Context

See proposal.md - Why. `XPBDGpuSolver` currently records a counted `ClearEntryValues` dispatch before every contact position and velocity accumulation. Its per-contact shaders then skip writes for every non-contributing guard, relying on that clear to make unread slots inert. Removing the clear therefore requires a write-semantics change in the accumulate shaders, not just deletion of solver calls.

The joint solver already defines two numerically equivalent modes: one dispatch per color for large colors and one single-workgroup sweep for small scenes. The sweep source exists but is excluded from the build. It binds 18 storage buffers and uses `memoryBarrierBuffer()`, which produces an `OpMemoryBarrier` that the pinned Tint version rejects. Four color metadata buffers are owned and uploaded by `PhysicsScene`; its CPU-side fixed and hinge ranges are also used to record per-color offsets.

This change follows `optimize-webgpu-submission-path` so its dispatch-count and timing measurements have a stable backend baseline.

## Goals / Non-Goals

**Goals:**

- Preserve the solver's constraint order and numerical behavior while eliminating redundant dispatch work.
- Make both joint modes consume one compact set of color metadata under the browser's 16-storage-buffer limit.
- Restore the sweep as a normal SPIR-V/WGSL artifact with explicit workgroup-scope synchronization.
- Verify both modes and the contact initialization through native and browser correctness paths.

**Non-Goals:**

- No reduction/sort algorithm redesign, global multi-workgroup barrier, persistent solver kernel, or elevated device limit.
- No change to substep, position-iteration, velocity-iteration, coloring, or large-color fallback policy.
- No requirement for bit-identical floating-point output across GPUs or dispatch implementations.
- No permanent performance tooling or universal millisecond target.

## Decisions

### D1. Each contact invocation initializes exactly the slots it owns

Add a slot-initialization helper beside `scatter_slot` in `xpbd_scatter.glsl`. It writes zero to the six linear/angular channels and the flag channel for one slot, using the existing channel-major capacity stride.

In both contact accumulate shaders, retain the collision-count and contact-capacity guards first. An invocation that owns a live contact then computes `slot_a` and `slot_b` and initializes both immediately, before shape/body validation or any other contact-specific early return. The existing constraint evaluation and scatter remain unchanged. A later scatter overwrites all seven channels for a contributing side; every early exit leaves two zero records.

The count/capacity guards may remain before initialization because slots at or beyond the published entry count are never read. Initialization must not touch contact lagrange, tangent-lagrange, or tangent-impulse records, whose lifetime remains one substep.

Remove `clear_values_kernel`, its loading, the push structure used only by that kernel, both `dispatch_clear_values` call sites, and `clear_entry_values.comp`. Do not add a replacement clear abstraction.

*Alternative: a fused multi-buffer clear.* Rejected because it retains dispatch overhead and another resource-layout contract without reducing the writes that accumulation already owns.

### D2. PhysicsScene publishes two concatenated color tables

Keep `m_fixed_joint_colors` and `m_hinge_joint_colors` unchanged on the CPU. Replace the four GPU color buffers with scene-owned `m_gpu_joint_color_ids` and `m_gpu_joint_color_offsets`, exposed through `PhysicsGpuBuffers` as shared pointers.

On `colors_rebuilt`, construct two temporary vectors:

```text
ids     = fixed.ids || hinge.ids
offsets = fixed.offsets || hinge.offsets
```

Reserve exact sizes, append complete ranges, ensure grow-only GPU capacity, and upload each table once. Empty segments are appended as their actual empty IDs range or one-entry `{0}` offsets range. When both ID segments are empty, use the existing one-element sentinel allocation convention; do not append per-segment ID sentinels. Rebuild and upload both complete tables after any Ensure reallocation.

The CPU ranges exposed for dispatch geometry remain the independent fixed and hinge spans. They are refreshed with the buckets and are not cached across a scene GPU synchronization.

*Alternative: upload four segments into shared buffers.* Correct but easier to get wrong around empty segments and partial reallocation; topology-only concatenation is preferable to per-frame optimization. Retaining six buffers was rejected because it duplicates persistent data and upload paths.

### D3. Shared-table indexing uses elements and existing constants

Let `Cf = fixed_color_count`, `F = offsets[Cf]`, and `B = Cf + 1`. `F` is the alive fixed-ID count, not the fixed descriptor slot capacity.

The sweep reads fixed ranges directly from `offsets[c]` and `offsets[c + 1]`. For hinge color `c`, it reads local ranges from `offsets[B + c]` and `offsets[B + c + 1]`, then indexes `ids[F + i]`. Its bindings 14 and 15 become `JointColorIds` and `JointColorOffsets`; bindings 16 and 17 disappear. The other 14 storage bindings and the 32-byte `JointSweepPush` layout stay unchanged, with a compile-time size assertion.

The per-color shaders remain unchanged. Fixed dispatch offsets remain `fixed_ranges[color]`. Hinge dispatch offsets become `fixed_ranges[s.fixed_colors] + hinge_ranges[color]`. Both bind the full shared IDs buffer at descriptor offset zero, avoiding storage-binding alignment requirements that a tightly packed descriptor subrange would incur.

### D4. Restore the existing mode selection and workgroup barrier semantics

Load and create the sweep kernel alongside the per-color kernels. When the existing `joint_sweep_max_color_width` policy selects sweep mode and every fixed and hinge color fits, record one sweep dispatch per position iteration. That workgroup walks fixed colors first and hinge colors second in the established order. Otherwise, execute the existing per-color loops without changing dispatch geometry or barriers.

Replace the two `memoryBarrierBuffer(); barrier();` pairs with a source-level workgroup-scope control barrier followed by the existing execution barrier:

```glsl
#extension GL_KHR_memory_scope_semantics : require

controlBarrier(
    gl_ScopeWorkgroup,
    gl_ScopeWorkgroup,
    gl_StorageSemanticsBuffer,
    gl_SemanticsAcquireRelease
);
barrier();
```

The pinned Tint accepts the resulting `OpControlBarrier` and emits `storageBarrier(); workgroupBarrier();`. Keep every invocation in the outer color loops and retain guarded work blocks so all barriers are reached uniformly. Do not enable the Vulkan memory model, modify generic SPIR-V rewriting, upgrade Tint, or maintain handwritten WGSL.

Remove the sweep from `PHYSICS_GLSL_DEFERRED` and remove obsolete deferred-artifact cleanup once normal per-source outputs own its lifetime.

*Alternative: a multi-workgroup color loop with shader barriers.* Invalid for portable WebGPU because those barriers synchronize only one workgroup; the existing per-color fallback remains the correct large-color path.

### D5. Verify indexing and numerical equivalence independently

First land contact initialization and run existing contact regressions with targeted early-out cases. Then migrate shared tables while still using only per-color mode, proving that fallback indexing alone preserves behavior. Finally enable the sweep and its barrier conversion.

Port the deferred `xpbd_joint_sweep_equivalence_test` to portable command-buffer creation, copy, submit/wait, and readback interfaces. Compare sweep-enabled and per-color scenes over at least 30 steps, checking that motion actually occurs. Cover mixed fixed+hinge joints, dead fixed descriptors so `F != fixed_joint_count`, fixed-only, hinge-only, no-joint, and a large color that forces fallback. Run this coverage on Vulkan and desktop WebGPU, and retain the actuator stand regression as an end-to-end check.

Add a finite browser joint smoke using the same mixed-joint fixture. It must execute the sweep, report an explicit numerical pass/fail, and surface WebGPU validation errors; a screenshot alone is not acceptance. Browser storage-interface inspection or pipeline creation must also confirm the 16-buffer budget.

In the reference scene, removing 120 clear dispatches and replacing 400 hinge-color dispatches with 40 sweeps gives an expected total near 740 rather than 1220. Treat that count as the structural target; record native Release timings after the count is confirmed.

## Risks / Trade-offs

- [An early return bypasses slot initialization] → Place initialization immediately after count/capacity guards and test every invalid/dead/kinematic/zero-impulse path with scratch pre-filled to non-zero values.
- [Initialization accidentally resets substep multipliers] → Restrict the helper to the seven scratch channels and run friction/restitution regressions across multiple substeps.
- [Hinge indexing uses descriptor capacity instead of alive fixed IDs] → Derive and test `F` from the final fixed range with dead fixed descriptors present.
- [Shared-table rebuild leaves stale or partial data] → Concatenate and upload complete tables on every color rebuild, including after buffer reallocation, and test topology changes.
- [A sweep barrier is non-uniform or has the wrong memory scope] → Preserve uniform outer loops, inspect generated WGSL for both barrier forms, and compare every step against per-color mode.
- [Single-workgroup sweep loses parallelism on large scenes] → Keep the existing width-based fallback; this change ports the mode rather than extending its applicability.
- [Native tests pass but the browser exceeds baseline limits] → Audit interface descriptions and run the finite browser smoke without requesting elevated limits.
- [Extra zero stores cost more than the removed clear] → The stores replace the same live-slot writes previously done by the clear; confirm the dispatch reduction and native phase timing before accepting the result.

## Migration Plan

1. Add contact initialization, remove the standalone clear, and verify contact numerical regressions and dispatch counts.
2. Introduce shared GPU color tables and migrate only per-color indexing; verify Vulkan and desktop WebGPU fallback behavior before enabling sweep.
3. Change sweep bindings and indexing, apply source-level workgroup barriers, restore normal shader baking, and enable the existing mode selection.
4. Restore portable equivalence tests and add the finite browser smoke. Roll back sweep enablement independently if necessary; shared tables remain valid for per-color mode.
5. Run the standard Vulkan and desktop WebGPU suites, browser correctness, and native Release PhysicsApp measurement. Confirm approximately 740 reference dispatches and no solver-quality regression.
