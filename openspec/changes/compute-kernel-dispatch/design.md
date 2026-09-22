# Design — compute-kernel-dispatch

See `proposal.md` — Why for the motivation. This document records the decisions that shape the call surface, the traps it must avoid, and what is deliberately left to other changes.

## Context

### Current state

Three facts from the existing code determine the design:

1. **The name→slot mapping already exists and is already automatic.** `SPLayout::Reflect` reads `spv::DecorationDescriptorSet` / `DecorationBinding` off each reflected interface (`ShaderParameterLayout.cpp:411-412`, `:430-431`) and fills `interface_name_mapping` (`:417`, `:436`). No C++ source states a compute binding number; callers write the GLSL block name (`srb.BindBuffer("RigidBodyAlive", ...)`).
2. **Every dispatch re-derives that mapping the expensive way and validates nothing.** Bound names live in `std::map<std::string, InterfaceVariant>` (`ShaderResourceBinding.cpp:27`); the descriptor-set cache key is a hash over the whole map, so each dispatch hashes 7–20 `std::string` keys through an ordered map. A declared interface with no bound name is skipped (`ShaderResourceBinding.cpp:117-120`), leaving an unwritten descriptor and no diagnostic.
3. **The stack is per-owner and duplicated.** ~40 `ComputeStage` instances exist; the same shader is loaded by two components independently (`clear_int_buffer.comp` by both `XpbdGpuSolver` and `ConvexCollisionDetector`), each with its own pipeline and descriptor pool; eight near-identical `LoadPhysicsSpirv*` helpers are copy-pasted across `engine/Physics/`.

### Frame-independent constraint carried forward

`rhi-compute-resource-binding` forbids render-frame vocabulary in the Rhi compute API. That constraint does not die with the class that encoded it: it is restated in `rhi-compute-kernel` as *No render-frame vocabulary in the kernel interface*, which additionally forbids any slot/rotation parameter on dispatch — the mechanism the old vocabulary existed to name.

### Cross-change conventions

- **Delta basing.** This change is C of a four-change sequence. Where a capability is also modified by change A (`gpu-buffer-retirement`) or change B (`descriptor-arena-epoch-buckets`), its `## MODIFIED Requirements` block is based on the most recent delta text rather than on `openspec/specs/`. In practice only `rhi-module` is shared with A: this change's block reproduces A's retirement-facility addition verbatim and adds the kernel.
- **Dependencies.** Change A makes record-time reallocation safe and is what allows a kernel to be acquired lazily during recording. Change B supplies the descriptor arena from which a kernel's sets are obtained; B is being written in parallel, so this change references it by name only and states no requirement about its internals.
- **Behavior is frozen.** This change re-plumbs how a dispatch is issued. It changes no algorithm's output, no buffer sizing, no push-constant content, and no barrier placement.

## Goals / Non-Goals

**Goals**

- Make one dispatch a single call: kernel identity plus a name→resource dictionary plus a grid.
- Turn the reflected interface table into the single source of truth for binding, and make a mismatched dictionary impossible to ignore.
- Collapse per-owner duplication (pipelines, pools, shader-module loading) into device-level facilities.
- Delete `ComputeStage`, `ComputeResourceBinding` and `ComputeHelpers` once nothing uses them, without disturbing the material path.

**Non-Goals (design-level)**

- No automatic barriers and no access tracking. Barrier placement stays manual (see D5).
- No rotation/slot concept of any kind, including a "prepared binding state" that a caller could pin across dispatches.
- No runtime shader hot reload (see D7).
- No grow-only resizing, no push-constant count migration, no deletion of `PreGPUStep` / `PostGPUStep`, no detector `Configure` folding — all owned by `physics-step-simplification`.
- No change to `ShaderResourceBinding`, `IndexedBuffer`, `StructuredBuffer` or `StructuredBufferPlacer`: the material path depends on them.
- No uniform-buffer *variable* support in the kernel (named-block member packing). Neither physics nor bloom needs it; every physics UBO interface is absent and bloom binds only images.
- No `DispatchIndirect` and no GPU-driven dispatch geometry.

## Decisions

### D1: Kernel identity is the SPIR-V module, and the cache is device-level

A kernel is keyed by the SPIR-V words it was created from, lives as long as the device context, and is requested lazily. One module therefore yields exactly one pipeline and one descriptor-set layout no matter how many components dispatch it.

**Alternatives rejected:** *Per-owner instantiation* (today's `ComputeStage`) — it is the direct cause of duplicate pipelines and pools for a shared shader. *A registry keyed by a caller-supplied name string* — two components could pick the same name for different modules and silently share the wrong pipeline; the module itself cannot collide.

### D2: Names are resolved to a dense table once, at kernel creation

At creation, each declared interface name is looked up in the reflected `interface_name_mapping` and stored in a dense table alongside its binding number, resource kind and push-constant size. Dispatch walks the dictionary once, mapping each supplied name to its table entry, and then hashes only `(binding number, handle, offset, size)`.

The dispatch-time name association still touches the short `string_view` keys the caller wrote, but it no longer allocates, no longer builds or hashes an ordered `std::map<std::string, ...>`, and no longer re-derives the layout hash from the bound set. The cost becomes proportional to the number of interfaces the caller binds rather than to a map of every interface the shader declares.

**Alternative rejected:** keeping the current content-hash-of-everything key. It is the reason each dispatch pays for the whole interface map, and it is what makes the descriptor-set cache unbounded — the sibling problem change B exists to remove.

### D3: A dictionary entry is a buffer or a texture

Buffers may carry an offset and size restricting the bound range; textures may carry a subresource range. This is not speculative generality: the bloom pass binds `inputImage` and `outputImage` as textures (`ComplexRenderGraphBuilder.cpp:222-227`), and bloom must migrate in this change or `ComputeResourceBinding` keeps a user and cannot be deleted.

**Alternative rejected:** a buffer-only dictionary with bloom left on the old API. It would preserve the three-layer stack indefinitely, defeating the change's purpose, and it would leave two compute paths whose descriptor lifetime policies differ.

### D4: Validation throws `std::runtime_error`

Dispatch throws when the dictionary names an undeclared interface and when it omits a declared one, and the message identifies the shader and the offending interface name.

**Alternatives rejected:** *Debug-only assert* — release builds would keep silently binding nothing, which is exactly today's failure mode. *Warn and skip* — a warning on a per-dispatch path is noise, and the unwritten descriptor still produces undefined reads rather than a diagnosable error. *Validating only in a debug build while skipping the write in release* — the two builds would differ in behavior, which is worse than either.

The cost is real and accepted: latent mismatches that compile and run today will throw. That is the point — they are currently invisible.

### D5: The kernel inserts no barrier, and the responsibility boundaries are stated

```
  inside one algorithm's recording call        -> the algorithm
  between two recording calls, same instance   -> the caller
  between two different kernels                -> the caller
  inside Dispatch                              -> nobody
```

An algorithm instance owns its scratch buffers, so two consecutive recording calls on one instance share them and need a barrier between them; a caller that wants to interleave two algorithms without a barrier uses two instances and pays for two sets of scratch.

**Alternative rejected:** inferring barriers from the dictionary's declared access intent. It was an explicit user decision to keep barrier placement manual; independently, correct inference needs a per-buffer last-access ledger that survives across dispatch sites, which is a feature of its own scale and would silently change synchronization the physics author deliberately placed.

### D6: Descriptor sets come from the change-B arena, re-acquired on every dispatch

A dispatch obtains the set for its `(layout, resources)` combination from the descriptor arena. There is no per-kernel binding state and no caller-declared rotation depth.

The acquisition happens **on every dispatch**, which is what satisfies the arena's re-acquisition contract. The arena keeps sets resident and reuses them across epochs, and it can treat an entry's recorded epoch as an upper bound on the epochs that reference it only if every user refreshes that record whenever it binds. A kernel therefore holds no set handle between dispatches — so there is nothing for a kernel to release, and nothing that can go stale underneath it.

There is no `slot_count`. Rotation existed so that one long-lived set could serve several in-flight batches; a resident set that is re-acquired every epoch and reclaimed only under cache pressure needs no rotation, and a caller-declared depth would be a number with nothing to size.

Within one epoch and across epochs the arena's content key makes the substep and iteration loops hit the same set — the loops that would otherwise pay an allocation hundreds of times per frame.

**Alternatives rejected:** *keeping `slot_count` under a new name* — it would reintroduce a caller-declared in-flight depth that the arena derives from the epoch protocol instead. *Caching the set handle in the kernel between dispatches* — it would break the re-acquisition contract and let the arena evict a set the kernel still binds.

### D7: A kernel is immutable; changed SPIR-V is a different kernel

A kernel's pipeline, layout and resolved table never change after creation. Shader hot reload is out of scope; SPIR-V whose contents change is a distinct module identity and therefore a distinct kernel.

**Alternative rejected:** a mutable kernel with a reload entry point. Pipeline swap-in-place requires deciding what happens to already-recorded command buffers holding the old pipeline, which is a lifetime problem this change does not have the machinery to answer.

### D8: The push-constant value keeps its current shape

Dispatch receives the push value as a typed value (the existing `sizeof(T)`-based helper shape), recorded at offset 0, with the debug assertion that it fits the reflected block size preserved. The reflected `push_constant_size` is what the kernel declares as its pipeline-layout range.

**Alternative rejected:** packing arguments by name from a dictionary, reusing the dormant `StructuredBuffer` + `StructuredBufferPlacer` machinery. It is a genuinely larger feature (named uniform-block variables), physics uses push constants exclusively, and the kernel deliberately does not support uniform-buffer variables (Non-Goals).

### D9: One shared SPIR-V loader

The eight `LoadPhysicsSpirv*` copies collapse into one helper used by the solver, both detectors and every `gpu_algorithm` class. Its contract is unchanged from today's: resolve the source-relative path against `ENGINE_PHYSICS_SPIRV_DIR`, and throw a `std::runtime_error` naming the absolute path attempted when the file is missing, empty, or not a multiple of four bytes.

**Alternative rejected:** leaving the copies and only changing what they are called with. The dedup is free once every caller is touched anyway, and eight copies of a diagnostic contract drift.

### D10: Bloom migrates, and the `CommandBuffer` compute wrappers are deleted

`CommandBuffer::BindComputeStage` / `BindComputeResource` / `DispatchCompute` and the `m_bound_compute_stage` member exist to wrap the deleted helpers and to carry the bound stage's pipeline layout for push-constant recording. Bloom is their only compute user. After bloom migrates to kernel dispatch they have no subject and are deleted with the rest of the stack.

Note the asymmetry that makes this safe: `CommandBuffer`'s *graphics* and *material* paths are untouched, and `PushConstants` for compute has no remaining caller because bloom records no push constants.

**Alternative rejected:** re-typing the wrappers onto the kernel. That would duplicate the dispatch surface and keep `m_bound_compute_stage` alive for a check the kernel already performs on its own pipeline layout.

### D11: Behavior is frozen, and the boundary with change D is explicit

Every difference this change introduces is in *how a dispatch is issued*. Nothing about *what is dispatched* moves: buffer sizing stays exact-size (no grow-only), push constants keep their current contents and C++ layouts, `PreGPUStep` and `PostGPUStep` keep their current responsibilities, detectors keep `Configure`, and every barrier stays where it is.

**Rationale:** this is the largest-diff change of the four (261 binding sites, ~35 dispatch sites, 9 components). Separating "the API changed" from "the behavior changed" is what makes a regression attributable to one or the other.

### D12: Spec-delta scope rule — include where the block is current, defer where it is already stale

The change deletes three types, so every capability that names them is nominally affected. The rule applied here:

- **Include** a capability when the requirement block affected by this change is otherwise current, so that the `## MODIFIED` block this change writes is truthful.
- **Defer** a capability whose affected block is stale for reasons another change owns, because a `## MODIFIED` block replaces the whole requirement and would cement that unrelated staleness as current.

Deferred, with the reason and where the sweep lands:

| Capability | Stale because | Swept by |
|---|---|---|
| `gpu-parallel-scan` | its dependency requirement names `RenderSystem` and `RenderGraphBuilder`, removed from physics long ago | `physics-step-simplification` |
| `gpu-convex-collision-detection` | describes "self-owned RenderGraph recording" | `physics-step-simplification` |
| `spatial-hash-broad-phase` | describes "self-owned RenderGraph recording" | `physics-step-simplification` |
| `xpbd-solver-multi-rg` | describes `PreGPUStep` responsibilities and a no-allocation rule that change D rewrites wholesale | `physics-step-simplification` |

These four are the only places where a reference to a deleted type survives this change. The sweep is a task in change D, and it must run before that change archives.

**Alternative rejected:** including all sixteen nominally-affected capabilities. It would multiply the delta set for no behavioral gain and, in the four cases above, would either cement stale text or force unrelated debt into this change.

**Purpose bookkeeping.** Two capabilities' `## Purpose` sections named the deleted types. `rhi-push-constants` keeps a live requirement (push-constant reflection in `SPLayout`), so its Purpose was edited in place under `openspec/specs/rhi-push-constants/spec.md` — a delta's Purpose is ignored for an existing capability, so the edit has to be made there. `rhi-compute-resource-binding` loses every requirement and is not edited: its fate is the archive step's decision, and a Purpose describing a capability that no longer exists should not be rewritten to describe its replacement, which lives in `rhi-compute-kernel`.

## Risks / Trade-offs

- **[The migration is wide: 261 binding sites and ~35 dispatch sites]** → Mitigation: migrate in dependency order (algorithms → detectors → solver → bloom), one component per commit, with each component's existing headless fixture as the gate. No component's behavior changes, so a fixture failure after a component's migration localizes to that component's call-site rewrite.
- **[Throwing on mismatch surfaces latent bugs mid-migration]** → Mitigation: migrate one component at a time and fix dictionaries rather than weakening validation. Expect the first run of each migrated component to fail loudly; that failure is the deliverable, not an obstacle. The diagnostic names both the shader and the interface so the fix is mechanical.
- **[Per-dispatch name association still costs a small hash over the supplied names]** → Mitigation: measured against today's cost (whole-map hash through an ordered map with `std::string` keys), it is strictly smaller and allocation-free. If it still shows up, a prepared binding signature per (kernel, resource-set shape) is a pure optimization that changes no contract — recorded as an open question.
- **[Kernel dedup changes who owns the pipeline]** → Mitigation: pipelines move to the device-level cache and descriptor pools to change B's arena; no kernel holds a pool. Ordering matters: change B must land first, otherwise the kernel would reintroduce a per-kernel pool.
- **[Deleting the `CommandBuffer` compute wrappers touches the render module]** → Mitigation: bloom is the only compute user and migrates inside this change; the graphics and material paths and `MaterialInstance` are untouched.
- **[Collapsing eight loaders could change an error string a test asserts on]** → Mitigation: the contract is preserved verbatim (throw, message includes the absolute path). Any test asserting on the message is checked before the copies are removed.
- **[Deferred capabilities keep a reference to a deleted type for one change cycle]** → Mitigation: D12's table names all four, the reason, and the owning change; the sweep is a task in change D that must complete before D archives.

## Migration Plan

1. **Kernel facility.** Add the kernel, its resolved interface table, the dictionary dispatch, validation, and the device-level cache. Nothing calls it yet. Verify: a headless unit test dispatches one kernel by dictionary, and the mismatch cases throw.
2. **Loader.** Collapse the eight `LoadPhysicsSpirv*` copies into one shared helper; keep every existing caller's behavior identical. Verify: the existing physics suites pass unchanged.
3. **Algorithms.** Migrate `ParallelScan`, `RadixSort`, `SumByKey`, `CompactUnique` to kernel dispatch. Verify: their headless GPU fixtures pass unchanged (`gpu_parallel_scan`, `gpu_radix_*`, `gpu_sum_by_key`, `gpu_compact_unique`).
4. **Detectors.** Migrate `SpatialHashBroadDetector` and `ConvexCollisionDetector`, including `Configure` acquiring kernels and `Record` creating no pipeline. Verify: the detector-backed physics fixtures pass.
5. **Solver.** Migrate `XpbdGpuSolver` (the ~141 binding call sites) and `DummySolver`. Verify: the physics app stability fixture and the XPBD headless fixtures pass.
6. **Bloom and deletion.** Migrate the bloom pass to kernel dispatch; delete `ComputeStage`, `ComputeResourceBinding`, `ComputeHelpers`, and the `CommandBuffer` compute wrappers. Verify: the project builds with no reference to the deleted types anywhere, and a windowed run renders the bloom pass.

Rollback is per-step: steps 1–2 are purely additive; steps 3–5 are per-component and independently revertible; step 6 is the only irreversible one and happens last, when no caller remains.

## Open Questions

- **Prepared binding signature.** A reusable object capturing a kernel plus a resource-set shape would remove the per-dispatch name association entirely. It is a pure optimization — the contract and the specs are unchanged either way — so it can be added later if profiling asks for it.
- **Named uniform-block variables in the kernel.** The dormant `StructuredBuffer` + `StructuredBufferPlacer` path already supports setting uniform-block members by name, and the kernel deliberately does not expose it. Revisit only if a compute kernel needs named variables; nothing in physics or bloom does today.
- **`CommandBuffer` compute push constants.** After bloom migrates, the render-side compute path records no push constants. If nothing else needs it, the remaining push-constant plumbing on `CommandBuffer` can be narrowed in a later cleanup rather than in this change.
