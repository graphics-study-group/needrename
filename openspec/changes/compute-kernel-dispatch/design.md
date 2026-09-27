# Design — compute-kernel-dispatch

See `proposal.md` — Why for the motivation. This document records the decisions that shape the call surface, the traps it must avoid, and what is deliberately left to other changes.

## Context

### Current state

Three facts from the existing code determine the design:

1. **The name→slot mapping already exists and is already automatic.** `SPLayout::Reflect` reads `spv::DecorationDescriptorSet` / `DecorationBinding` off each reflected interface (`ShaderParameterLayout.cpp:411-412`, `:430-431`) and fills `interface_name_mapping` (`:417`, `:436`). No C++ source states a compute binding number; callers write the GLSL block name (`srb.BindBuffer("RigidBodyAlive", ...)`).
2. **Every dispatch re-derives that mapping the hard way and validates nothing.** Bound names live in an ordered `std::map<std::string, InterfaceVariant>` (`ShaderResourceBinding.cpp:28`), and every dispatch walks the whole reflected interface table to look its own bound names up there and to build the resolved-content vector the arena consumes. The set cache, its content hash and its reclamation left this class in change B, which has landed; what remains per dispatch is the traversal, the ordered-map lookups and that vector. A declared interface with no bound name is still skipped (`ShaderResourceBinding.cpp:68-71`), leaving an unwritten descriptor and no diagnostic.
3. **The stack is per-owner and duplicated.** 42 `ComputeStage` instances exist; the same shader is loaded by two components independently (`clear_int_buffer.comp` by both `XPBDGpuSolver` and `ConvexCollisionDetector`), each with its own pipeline and descriptor pool; eight near-identical `LoadPhysicsSpirv*` helpers are copy-pasted across `engine/Physics/`, with a ninth in a headless test.

### Frame-independent constraint carried forward

`rhi-compute-resource-binding` forbids render-frame vocabulary in the Rhi compute API. That constraint does not die with the class that encoded it: it is restated in `rhi-compute-kernel` as *No render-frame vocabulary in the kernel interface*, which additionally forbids any slot/rotation parameter on dispatch — the mechanism the old vocabulary existed to name.

### Cross-change conventions

- **Delta basing.** This change is C of a four-change sequence. Where a capability is also modified by change A (`gpu-buffer-retirement`), change B (`descriptor-arena-epoch-buckets`) or `stable-buffer-identity`, its `## MODIFIED Requirements` block is based on the text those landed changes left in `openspec/specs/`, not on an earlier plan. Three of this change's blocks overlap them: `rhi-module` carries A's retirement paragraph, the shared-ownership clause `stable-buffer-identity` rewrote, and B's descriptor-arena paragraph; `physics-gpu-shaders` carries `stable-buffer-identity`'s shared `model_matrix.comp` path; and `rhi-descriptor-arena` is B's own capability, whose compute-pipeline scenario this change re-expresses on the kernel.
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
- No multi-set compute kernel. A kernel binds set 0 only, which is where every compute shader in the engine declares its interfaces; the material path's sets 1 and 2 stay with `ShaderResourceBinding`. Serving several sets would mean one descriptor-set layout per set, one acquisition per set per dispatch, and a multi-set `bindDescriptorSets` call — none of which this change needs, and any of which would enlarge the arena's per-dispatch cost.
- No `DispatchIndirect` and no GPU-driven dispatch geometry.

## Decisions

### D1: Kernel identity is a caller-supplied module identity, and the cache is device-level

A kernel is keyed by the module identity its caller supplies — a shader asset's GUID, or a directly loaded module's path — lives as long as the device context, and is requested lazily. One identity therefore yields exactly one pipeline and one descriptor-set layout no matter how many components dispatch it.

**Why not the module's contents.** The first draft of this decision keyed the cache on a hash of the SPIR-V words and kept the words to guard collisions. It was reversed on implementation review for three reasons: the lookup scans and compares the whole module even when it hits — debug modules in this repository reach 181 KB, so the identity computation was the most expensive part of a *cache hit* — retaining the words makes the device hold a copy of every module solely to verify a value the caller already knows, and the guard was only on the read path: the insert path re-used the key without re-checking, so two modules colliding on the hash would have left the cache inconsistent rather than handled. After `vkCreateShaderModule` the words are not needed by the facility again at all.

**Why a supplied identity is safe here.** It is not a hand-written debug name — the case the second alternative below rejects. It comes from whatever already owns the module: the asset manager's GUID, or the path the module was loaded from, and the debug name stays a separate argument. Equal identities must denote the same module; that assertion is the caller's and is never verified, which is why no call site chooses one — the two wrappers (`LoadPhysicsKernel` and the shader asset's `RequestComputeKernel`) derive it from the path and the GUID respectively.

**Ownership and teardown order.** The cache is a `DeviceContext` member, requested through the context and destroyed with it — never released while the device lives. It is declared after the immutable resource cache and the descriptor arena so that it is destroyed before them: a kernel owns a pipeline, a pipeline layout and a shader module (all of which need the device alive) and names a descriptor-set layout that the arena resolved and the resource cache owns. The cache is bounded by construction, because SPIR-V is fixed at build time and the number of distinct modules is therefore a build-time constant.

**Alternatives rejected:** *Per-owner instantiation* (today's `ComputeStage`) — it is the direct cause of duplicate pipelines and pools for a shared shader. *Keying on the module's contents* — see above. *A registry keyed by a free-form caller-supplied name* — two components could pick the same name for different modules and silently share the wrong pipeline; an identity derived from an asset GUID or a file path cannot.

### D2: Names are resolved to a dense table once, at kernel creation

At creation, each declared interface name is looked up in the reflected `interface_name_mapping` and stored in a dense table alongside its binding number, resource kind and push-constant size. Dispatch walks the dictionary once, mapping each supplied name to its table entry, and hands the arena the resulting `(binding number, handle, offset, size)` entries — which are what the arena's content key is computed over, not anything the kernel stores.

The dispatch-time name association still touches the short `string_view` keys the caller wrote: each dictionary entry is looked up once in the kernel's resolved table. What disappears is the walk over every declared interface, the ordered-map lookup per interface and the construction of the resolved-content vector, so the cost becomes proportional to the resources the caller binds rather than to the interfaces the shader declares. The content hash the arena computes over the resolved entries stays — that key is the arena's, not this change's subject.

**One resolver, two callers.** The kernel needs the same interface→`ResolvedBinding` decision `ShaderResourceBinding` makes today: the reflected interface fixes the descriptor type, a sampled image maps to `eReadOnlyOptimal` and a storage image to `eGeneral`, and the enforced dynamic flags pick the uniform/storage descriptor variant. That decision is factored out of `ShaderResourceBinding` into one resolver both callers use rather than copied into the kernel, and `ShaderResourceBinding`'s public contract and the material path's behaviour are unchanged — it is the only edit this change makes inside that class.

**Alternative rejected:** leaving the mapping inside `ShaderResourceBinding`'s ordered map on the compute path. The content cache, the hash-collision caveat and the unbounded growth that used to sit behind it were removed by change B, which moved them into the arena; what remains is the full-table traversal this change removes. *Copying the mapping into the kernel instead of sharing it* is rejected for the same reason the loader copies are: two copies of one decision drift.

### D3: A dictionary entry is a buffer or a texture

Buffers may carry an offset and size restricting the bound range; textures may carry a subresource range. This is not speculative generality: every bloom pass binds `inputImage` and `outputImage` as textures (`ComplexRenderGraphBuilder.cpp:214-219`, `EditorRenderGraphBuilder.cpp:233-241` and `:315-323`, and `pbr_test`'s copy), and all of them must migrate in this change or `ComputeResourceBinding` keeps a user and cannot be deleted.

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

**The arena's acquisition surface is what the kernel is written against.** A kernel resolves its descriptor-set layout once, at creation, through `DescriptorArena::ResolveLayout` — the arena's only layout source, so the object its pipeline layout is built over is the object a set is allocated against. Every dispatch then calls `DescriptorArena::Acquire(layout, set_id, enforce_dynamic_uniform, enforce_dynamic_storage, content)` with the flags its own layout description was created with, and hands it a `std::span<const Rhi::ResolvedBinding>` built from the dictionary: one entry per bound interface, carrying the reflected binding number, the descriptor type the reflected interface implies, the buffer handle with its offset and range, or the image view with the image layout the interface type requires (`eReadOnlyOptimal` for a sampled image, `eGeneral` for a storage image — the same mapping the material path's resolver uses).

Two consequences are contracts rather than implementation details, and both are stated in `rhi-compute-kernel`: the kernel MUST NOT resolve a layout description of its own, and the resolved entries MUST be ordered canonically by ascending binding number. The arena's content key is order-sensitive — it hashes the span in sequence and compares it element-wise (`DescriptorArena.cpp:163-166`, `:399-401`) — so a dictionary whose order leaked into the entry vector would mint a distinct set per order and defeat the reuse the arena exists to provide. Emitting the entries in the reflected table's order, the way the current resolver does by iterating `SPLayout::interfaces`, is what keeps the key stable.

**Uniform buffers are bound statically.** The kernel generates its layout with `SPLayout::GenerateLayoutBindings(0, false, false)` — set 0, no enforced dynamic offset — so a buffer entry's offset and size become the descriptor range, exactly as a storage buffer's do, and a dispatch produces no dynamic offsets at all. The deleted `ComputeStage` enforced dynamic uniform buffers because `ComputeResourceBinding` rotated uniform-buffer slices through an `IndexedBuffer`; nothing in the kernel rotates, `ComputeResourceBinding` is deleted, and no physics shader declares a non-push uniform block — the only compute UBO left is the fluid test's `{ uint frame_count; }`.

**Alternatives rejected:** *keeping `slot_count` under a new name* — it would reintroduce a caller-declared in-flight depth that the arena derives from the epoch protocol instead. *Caching the set handle in the kernel between dispatches* — it would break the re-acquisition contract and let the arena evict a set the kernel still binds. *Dynamic uniform buffers with a per-dispatch offset list* — it buys nothing a caller can use, because no compute consumer rotates a UBO, and it turns the dictionary's offset into a bind-time offset whose order must then be defined against the layout's binding order, reintroducing the question the old code left open (`// FIXME: Dynamic offset order might not be correct.`).

### D7: A kernel is immutable; changed SPIR-V is a different kernel

A kernel's pipeline, layout and resolved table never change after creation. Shader hot reload is out of scope; a module whose SPIR-V changes is reached through a new identity, and the facility offers no way to rebuild or mutate an existing kernel.

**Alternative rejected:** a mutable kernel with a reload entry point. Pipeline swap-in-place requires deciding what happens to already-recorded command buffers holding the old pipeline, which is a lifetime problem this change does not have the machinery to answer.

### D8: The push-constant value keeps its current shape

Dispatch receives the push value as a typed value (the existing `sizeof(T)`-based helper shape), recorded at offset 0, with the debug assertion that it fits the reflected block size preserved. The reflected `push_constant_size` is what the kernel declares as its pipeline-layout range.

**Alternative rejected:** packing arguments by name from a dictionary, reusing the dormant `StructuredBuffer` + `StructuredBufferPlacer` machinery. It is a genuinely larger feature (named uniform-block variables), physics uses push constants exclusively, and the kernel deliberately does not support uniform-buffer variables (Non-Goals).

### D9: One shared SPIR-V loader, in the physics module

The eight component-local copies under `engine/Physics/` — and a ninth in `test/engine/headless/gpu_compact_unique_test.cpp`, which loads the broad phase's `unpack_pairs.comp` itself — collapse into one helper in `engine/Physics/` used by the solver, both detectors, every `gpu_algorithm` class and that test. It cannot live in `Rhi`: it resolves a path against `ENGINE_PHYSICS_SPIRV_DIR`, a physics build-tree macro, and the Rhi module must not learn about physics to read a file.

Its contract is unchanged from today's: resolve the source-relative path against `ENGINE_PHYSICS_SPIRV_DIR`, and throw a `std::runtime_error` naming the absolute path attempted when the file is missing, empty, or not a multiple of four bytes. Because the helper compiles inside `EnginePhysics`, it carries that target's per-configuration `ENGINE_PHYSICS_SPIRV_CONFIG_SUBDIR` with it, so the test no longer needs the `target_compile_definitions` workaround that exists only to hand it the same subdirectory — a second reason to route it through the helper instead of leaving its copy in place.

**Alternative rejected:** leaving the copies and only changing what they are called with. The dedup is free once every caller is touched anyway, and nine copies of a diagnostic contract drift.

### D10: Bloom migrates, and the `CommandBuffer` compute wrappers are deleted

`CommandBuffer::BindComputeStage` / `BindComputeResource` / `DispatchCompute` and the `m_bound_compute_stage` member exist to wrap the deleted helpers and to carry the bound stage's pipeline layout for push-constant recording. Bloom is their only compute user. After bloom migrates to kernel dispatch they have no subject and are deleted with the rest of the stack.

Bloom exists in three places and all three migrate: the framework builder's pass (`ComplexRenderGraphBuilder.cpp:214-219`), the editor's scene and game widget passes (`EditorRenderGraphBuilder.cpp:97-104`, `:219-244`, `:301-326`), and `pbr_test`'s copy (`test/engine/windowed/pbr_test.cpp:286-353`). They load one shared SPIR-V module, so under D1 they resolve to one kernel — the dedup this change exists for, visible inside a single change.

Note the asymmetry that makes this safe: `CommandBuffer`'s *graphics* and *material* paths are untouched, and `PushConstants` for compute has no remaining caller because bloom records no push constants.

**Alternative rejected:** re-typing the wrappers onto the kernel. That would duplicate the dispatch surface and keep `m_bound_compute_stage` alive for a check the kernel already performs on its own pipeline layout.

### D11: Behavior is frozen, and the boundary with change D is explicit

Every difference this change introduces is in *how a dispatch is issued*. Nothing about *what is dispatched* moves: buffer sizing stays exact-size (no grow-only), push constants keep their current contents and C++ layouts, `PreGPUStep` and `PostGPUStep` keep their current responsibilities, detectors keep `Configure`, and every barrier stays where it is.

**Rationale:** this is the largest-diff change of the four (264 binding sites, ~35 dispatch sites, 9 components). Separating "the API changed" from "the behavior changed" is what makes a regression attributable to one or the other.

### D12: Spec-delta scope rule — include where the block is current, defer where it is already stale

The change deletes three types, so every capability that names them is nominally affected. The rule applied here:

- **Include** a capability when the requirement block affected by this change is otherwise current, so that the `## MODIFIED` block this change writes is truthful.
- **Defer** a capability whose affected block is stale for reasons another change owns, because a `## MODIFIED` block replaces the whole requirement and would cement that unrelated staleness as current.

`rhi-descriptor-arena` is included under the first rule: change B wrote its layout-sharing requirement, that block is current, and the scenario naming the compute pipeline object is re-expressed on the kernel rather than left for a later archive. It is B's capability rather than this change's, which is why that block reproduces B's requirement and both of its scenario names and changes only what the second scenario says the compute pipeline object is.

Deferred, with the reason and where the sweep lands:

| Capability | Stale because | Swept by |
|---|---|---|
| `gpu-parallel-scan` | its dependency requirement names `RenderSystem` and `RenderGraphBuilder`, removed from physics long ago | `physics-step-simplification` |
| `gpu-convex-collision-detection` | describes "self-owned RenderGraph recording" | `physics-step-simplification` |
| `spatial-hash-broad-phase` | describes "self-owned RenderGraph recording" | `physics-step-simplification` |
| `xpbd-solver-multi-rg` | describes `PreGPUStep` responsibilities and a no-allocation rule that change D rewrites wholesale | `physics-step-simplification` |

These four are the only places where a reference to a deleted type survives this change: the descriptor arena's equivalent reference is re-expressed by this change rather than deferred, for the reason just given. The sweep is a task in change D, and it must run before that change archives.

**Alternative rejected:** including every nominally-affected capability. It would multiply the delta set for no behavioral gain and, in the four cases above, would either cement stale text or force unrelated debt into this change.

**Purpose bookkeeping.** Two capabilities' `## Purpose` sections named the deleted types. `rhi-push-constants` keeps a live requirement (push-constant reflection in `SPLayout`), so its Purpose was edited in place under `openspec/specs/rhi-push-constants/spec.md` — a delta's Purpose is ignored for an existing capability, so the edit has to be made there. `rhi-compute-resource-binding` loses every requirement, and a Purpose describing a capability that no longer exists should not be rewritten to describe its replacement, which lives in `rhi-compute-kernel`. It is **retired** instead: `.openspec.yaml` declares `retire_capabilities: true`, which is what lets the archive delete `openspec/specs/rhi-compute-resource-binding/spec.md`. That marker is not optional bookkeeping — without it the archive refuses to write the rebuilt spec at all, because a capability may not end up with zero requirements and deleting the file is an act only the author can authorize.

**Residual staleness, recorded.** `detector-configure-detect`'s `Configure` requirement keeps two statements that `physics-push-constants` superseded: that `Configure` "creates the detector config uniform buffer", and that it writes `shape_slot_count` / `contact_margin` / `GridConfig` into the detector's host-visible GPU memory. Both are CPU members pushed at record time today (`ConvexCollisionDetector.cpp:220-228`, `SpatialHashBroadDetector.cpp:650-653`), and no detector holds a host-visible config buffer. The block is not corrected here because `physics-step-simplification` deletes `Configure` and that whole requirement (its tasks 4.3); rewriting text whose subject is about to be removed would misattribute the churn — the same call that change's own design records for the stale scenario titles it may not rename.

## Risks / Trade-offs

- **[The migration is wide: 264 binding call sites measured in `engine/Physics/` (132 of them in `XPBDGpuSolver.cpp` alone) and ~35 dispatch sites]** → Mitigation: migrate in dependency order (algorithms → detectors → solver → bloom), one component per commit, with each component's existing headless fixture as the gate. No component's behavior changes, so a fixture failure after a component's migration localizes to that component's call-site rewrite. The three bloom sites and the remaining compute tests are one final commit, gated by the windowed tests.
- **[Throwing on mismatch surfaces latent bugs mid-migration]** → Mitigation: migrate one component at a time and fix dictionaries rather than weakening validation. Expect the first run of each migrated component to fail loudly; that failure is the deliverable, not an obstacle. The diagnostic names both the shader and the interface so the fix is mechanical.
- **[Per-dispatch name association still costs a small lookup over the supplied names]** → Mitigation: measured against today's cost (a full pass over the reflected table with an ordered-map lookup per declared interface, plus the content vector), it is strictly smaller and proportional to the dictionary rather than to the shader's interface count. If it still shows up, a prepared binding signature per (kernel, resource-set shape) is a pure optimization that changes no contract — recorded as an open question.
- **[The dominant per-dispatch cost is the arena acquisition, not the name lookup]** → Accepted, and worth stating plainly: every dispatch hands `Acquire` a freshly built entry vector for it to hash and compare, and change B's re-acquisition contract requires exactly that. This change therefore does not claim a hot-path speedup; what it delivers is one call instead of ten, one pipeline per module instead of one per owner, and one loader instead of nine. If the content hash ever shows up in a profile, the fix belongs in the arena — an entry token a caller can refresh without re-hashing — and not in a kernel that caches sets.
- **[Kernel dedup changes who owns the pipeline]** → Mitigation: pipelines move to the device-level cache and descriptor pools to change B's arena; no kernel holds a pool. Ordering matters: change B must land first, otherwise the kernel would reintroduce a per-kernel pool.
- **[Deleting the `CommandBuffer` compute wrappers touches the render module]** → Mitigation: bloom is the only compute user and migrates inside this change; the graphics and material paths and `MaterialInstance` are untouched.
- **[Collapsing nine loaders could change an error string a test asserts on]** → Mitigation: the contract is preserved verbatim (throw, message includes the absolute path), and no test asserts on the text today: the only other copy of those messages is the headless test's own loader, which the consolidation removes.
- **[Deferred capabilities keep a reference to a deleted type for one change cycle]** → Mitigation: D12's table names all four, the reason, and the owning change; the sweep is a task in change D that must complete before D archives. The arena's reference is not among them: it is re-expressed here.
- **[A dictionary's order leaking into the arena's content key would mint a set per order]** → Mitigation: canonical ordering by binding number is a stated contract in `rhi-compute-kernel` with its own scenario, and the kernel builds its entry vector from the reflected table's order rather than from the caller's dictionary.
- **[A module identity reused for a different module would silently share the first kernel]** → Mitigation: the identity is never chosen at a call site. `LoadPhysicsKernel` derives it from the module's path and the shader-asset wrapper from the asset's GUID, both of which are unique per module by construction, and the debug name stays a separate argument. The contract is stated in `rhi-compute-kernel`, and the kernel carries the name of the requester that created it, so a surprising share is visible in the object names.
- **[The editor's bloom and the compute tests sit outside `engine/Physics/`]** → Mitigation: they are named in the Impact list and migrated in the last step; the build fails on any remaining reference to a deleted type, so none can be forgotten silently.

## Migration Plan

1. **Kernel facility.** Add the kernel, its resolved interface table, the dictionary dispatch, validation, the shared resolver of task 1.8, and the device-level cache wired into `DeviceContext` with the declaration order D1 states. Nothing calls it yet. Verify: a headless unit test dispatches one kernel by dictionary, and the mismatch cases throw.
2. **Loader.** Collapse the eight component-local copies and the headless test's ninth into one shared helper in `engine/Physics/`; keep every existing caller's behavior identical. Verify: the existing physics suites pass unchanged and a repository search finds one loader definition.
3. **Algorithms.** Migrate `ParallelScan`, `RadixSort`, `SumByKey`, `CompactUnique` to kernel dispatch. Verify: their headless GPU fixtures pass unchanged (`gpu_parallel_scan`, `gpu_radix_*`, `gpu_sum_by_key`, `gpu_compact_unique`).
4. **Detectors.** Migrate `SpatialHashBroadDetector` and `ConvexCollisionDetector`, including `Configure` acquiring kernels and `Record` creating no pipeline. Verify: the detector-backed physics fixtures pass.
5. **Solver.** Migrate `XPBDGpuSolver` (132 binding call sites in that file) and `DummySolver`. Verify: the physics app stability fixture and the XPBD headless fixtures pass.
6. **Bloom and deletion.** Migrate all three bloom passes (the framework builder's, the editor's two) and `pbr_test`'s copy to kernel dispatch; delete `ComputeStage`, `ComputeResourceBinding`, `ComputeHelpers`, and the `CommandBuffer` compute wrappers. Verify: the project builds with no reference to the deleted types anywhere, and a windowed run renders the bloom pass in the framework app and in the editor.

Rollback is per-step: steps 1–2 are purely additive; steps 3–5 are per-component and independently revertible; step 6 is the only irreversible one and happens last, when no caller remains.

## Open Questions

- **Prepared binding signature.** A reusable object capturing a kernel plus a resource-set shape would remove the per-dispatch name association entirely. It is a pure optimization — the contract and the specs are unchanged either way — so it can be added later if profiling asks for it. Reopening it means revising the non-goal above, because such an object is a prepared binding state even when it is safe: it would hold the resolved entry shape and the resources, and would still re-acquire its set on every dispatch.
- **An arena entry token.** The arena returns a set and its offsets but no way to say "the content I bound last epoch, again", so every dispatch rebuilds its entry vector and the arena re-hashes it. If that ever dominates a dispatch-heavy profile, the arena could mint an opaque entry token — not a set handle, so the re-acquisition contract is untouched — and accept it on a refresh call that re-records the epoch. That is an arena change and belongs to a follow-up rather than to this one.
- **Named uniform-block variables in the kernel.** The dormant `StructuredBuffer` + `StructuredBufferPlacer` path already supports setting uniform-block members by name, and the kernel deliberately does not expose it. Revisit only if a compute kernel needs named variables; nothing in physics or bloom does today.
- **`CommandBuffer` compute push constants.** After bloom migrates, the render-side compute path records no push constants. If nothing else needs it, the remaining push-constant plumbing on `CommandBuffer` can be narrowed in a later cleanup rather than in this change.
