# Design — stable-buffer-identity

See `proposal.md` — Why for the motivation. This document records the decisions that shape the mechanism, the traps it must avoid, and what is deliberately left out.

## Context

### Current state

Four facts from the code determine the design:

1. **A buffer's storage is replaced by replacing the buffer object.** Every `EnsureBuffer` copy compares the requested byte size against `GetSize()` and constructs a *new* `ComputeBuffer` when they differ (`PhysicsScene.cpp:33`, `XPBDGpuSolver.cpp:212`, `SpatialHashBroadDetector.cpp:165`, `ConvexCollisionDetector.cpp:91-123`). The old object is destroyed. Any reference to the facade — the render graph's `buffer_mapping`, `SceneDataManager`'s pointer — is left dangling by design.
2. **The render graph is built once and holds a raw pointer.** `RenderGraphBuilder::ImportExternalResource` stores `&buffer` and `RenderGraph2ExtraInfo::buffer_mapping` keeps it for the graph's lifetime. The barrier path resolves the handle at *record* time (the buffer field of the compiled `vk::BufferMemoryBarrier2` is left null and the record path is where the handle would be filled in), so a stable object address is exactly what that design wants.
3. **The graph's imported pointer is currently never dereferenced.** `RenderGraph::Record` collapses buffer barriers into a global `MemoryBarrier2` and carries a `TODO: Use BufferMemoryBarrier2`. What actually makes rendering correct today is that `SceneDataManager` rewrites descriptor binding 2 on every frame (`SceneDataManager.cpp:400-416`). So the shared reference added by `gpu-buffer-retirement` keeps alive a pointer that nothing reads, while the graph and the descriptor silently diverge after any resize.
4. **The model matrices buffer has exactly one writer, and it is produced as a side effect of a step.** `PhysicsScene::RefreshGpuBuffers` allocates it and never uploads initial data; the only writer is the model-matrix dispatch at the tail of `XpbdGpuSolver::GPUStep` (`:1082-1091`, recorded unconditionally, with no `IsSimulationEnabled` check). Three hosts therefore each rely on a different accident to keep it valid:

```
  MainClass::RunOneFrame   GPUStep is called unconditionally every frame        (:294)
  editor example           GPUStep is gated on m_is_playing (:316-318), so the
                           buffer is NOT written while stopped; the editor is safe
                           only because IsPhysicsActive() keeps model_mat_index
                           negative while stopped (RendererComponent.cpp:55) —
                           an independent, spec'd switch (physics-adaptor)
  PhysicsApp               a commit-time seed pass runs a whole step with
                           simulation disabled (PhysicsApp.cpp:746-774, D7 of
                           physicsapp-body-state-write) plus GPUStep inside Step()
```

### Constraints from existing specs and the module layout

- `physics-dll-module` requires every `engine/Physics/**/*.h` to keep zero dependency on `Framework/`, `Render/`, `Asset/` and `UserInterface/` headers. The new solver entry point therefore names `Rhi::ComputeBuffer` by forward declaration only.
- `rhi-module` pins `AllocatorState(DeviceInterface &)` and the standalone (no `DeviceContext`) setup as supported. The new RHI capability must work in both.
- Inter-module ordering is fixed by the frame loop: `FlushPhysics` (which sizes physics buffers) runs before `StartFrame`, and `StartFrame` is where `UploadSceneData` writes descriptor binding 2.
- Only one `RenderSystem` / `SceneDataManager` exists per process, and only the main scene is constructed with rendering *and* physics enabled (`WorldSystem.cpp:22-24`); `CreateScene()` makes scenes with both disabled. The editor's two widgets are two cameras over one global renderer list. **Two physics-enabled scenes cannot be rendered in one frame today**, so a per-scene model matrices binding is not reachable and is out of scope.

### Delta-expression constraint discovered while writing this change

`openspec validate` refuses a `MODIFIED` requirement that omits any scenario the current spec has ("archive refuses to drop them"), and a requirement header that does not match an existing one is treated as an addition — the old requirement survives. Consequences for this change's deltas:

- Where a requirement's *name* becomes wrong because the mechanism it names is deleted, the delta expresses it as `REMOVED` + `ADDED` with a new name (`ComplexRenderGraphBuilder accepts optional model matrices buffer`, `SceneDataManager receives model matrices buffer from the assembly layer`, `EditorRenderGraphBuilder accepts model matrices buffer`).
- Where only a scenario's premise disappears, the scenario name is retained and its body is rewritten, because dropping it is refused. Two such names are now queries answered negatively rather than statements: `Shared-ownership buffer factory available from Rhi` (rhi-module) and `Model matrix recorded unconditionally` (xpbd-solver-multi-rg). A future change that may legitimately drop them should rename the whole requirement instead.

## Goals / Non-Goals

**Goals**

- Make a long-lived reference to a buffer facade safe by construction, so that no ownership sharing is needed anywhere.
- Give the model matrices buffer exactly one owner, in the same lifetime domain as the render graph that holds a reference to it.
- Make "produce model matrices" an explicit, separately requestable operation instead of a side effect of a physics step.
- Delete the shared-ownership vocabulary introduced for this one buffer.

**Non-Goals (design-level)**

- No geometric growth policy and no change to what `GetSize()` means. That is `physics-step-simplification`'s `rhi-buffer-capacity` capability; this change only fixes the *mechanism* of replacement and leaves the *policy* of how much to grow to that change. In particular, the physics-side call sites keep their exact-size semantics here.
- No slot reuse, no physics delete support, no per-scene model matrices binding, no multi-queue submission.
- No change to the retirement protocol, the descriptor arena, the compute dispatch surface, or barrier placement of existing passes.
- No consolidation of `SubmissionHelper`'s staging containers.

## Decisions

### D1: The root cause is identity churn, not ownership

`gpu-buffer-retirement` solved the dangling pointer by sharing ownership. That is a fix for the symptom, and it is strictly weaker than fixing the identity:

- It does not make the graph reference the *current* buffer. The graph keeps the object it imported; the descriptor is rebound to the current one every frame. After a resize the two disagree, and the graph's share holds memory the renderer no longer uses (fact 3).
- It introduces the only reference-counted buffer in the engine, and with it a second ownership vocabulary that every reader of `PhysicsGetBuffers`/`SceneDataManager` has to learn.

The invariant this change establishes instead: **a buffer object never moves; only its storage is replaced.** Then a raw pointer into a buffer is as safe as a `std::vector` reference to a container that may reallocate internally, and the ownership question becomes a pure design choice rather than a safety requirement.

### D2: In-place storage replacement, and what it invalidates

```
  DeviceBuffer (protected)   ReallocateStorage(allocator, bytes)
                               allocation = allocator.AllocateBuffer(allocation.GetMemoryType(), bytes, m_name)
                               m_size = bytes
  ComputeBuffer (public)     Reallocate(allocator, bytes)      exact, may grow or shrink
                             EnsureCapacity(allocator, bytes)  grow-only
```

The mechanism is small because the pieces already exist: `BufferAllocation` is movable and its move assignment is already retirement-aware (`MemoryAllocation.cpp:91-98`: `Destroy()` on the old value hands it to the tracker, then the new value is adopted). The replacement type is recoverable from `allocation.GetMemoryType()`, so no creation flags need to be stored.

**The invalidation contract, stated explicitly** because it is the price of the design: the object address, and therefore any reference to the buffer, survives; `GetBuffer()` and `GetVMAddress()` obtained before the call do not, and contents are not preserved. Callers re-establish contents themselves (physics already re-uploads every SoA column on every sync).

**Alternatives considered:**

- *Keep replacing the object and make every holder re-resolve* — requires an indirection (a stable slot or provider) in the render graph and does not help any other long-lived reference. Rejected: more machinery for a weaker property.
- *Fixed capacity, never resize* — zero mechanism, but caps rigid bodies at an arbitrary constant and gives up the general property. Rejected as the primary answer; it remains a fallback for any buffer that must not be reallocated (see D5).

### D3: Two entry points, because there are two intents

`Reallocate` is exact-size and may shrink; `EnsureCapacity` never shrinks. The physics-side call sites need the first to preserve today's semantics — several consumers treat "capacity" as "logical element count", which is exactly the trap D5 of the *next* change has to unwind:

```
  SpatialHashBroadDetector.cpp:314/326   dispatch geometry derived from GetSize()
  contact_entries.comp:81, hinge_entries.comp:74, fixed_entries.comp:72
                                         rigid_body_alive.v.length() read as the live body count
  detect_collisions.comp:159             the write guard doubles as the configured budget guard
```

The render-owned model matrices buffer uses the second, because its demand is a slot count that rises and falls and only the maximum matters.

**Naming:** the exact-size entry point is deliberately not called `Resize`, because `std::vector::resize` preserves contents and this does not.

### D4: The debug name lives in the buffer facade, not in the allocation

A reallocation must reproduce the creation parameters. `BufferType` is recoverable from the allocation being replaced and `size` is the argument, but the name is not — so someone has to remember it.

**The buffer remembers it.** `DeviceBuffer` stores the name it was created with (`m_name`) and passes it to `AllocatorState::AllocateBuffer` when it replaces its storage, so a reallocation reproduces the name for free and the caller never repeats it.

**Why not the allocation** (the shape first considered): `BufferAllocation` is the piece that is destroyed and re-created on every reallocation, so putting the name there means paying for a `std::string` in the object that churns most, growing the pimpl of a type that exists to be short-lived. The buffer facade is created once and never replaced, which is exactly the lifetime a remembered creation parameter wants — and it is the object every caller already holds. The cost is one `std::string` member on a facade that would otherwise not hold it; the value is also only used by `setDebugUtilsObjectNameEXT`, and only in debug builds (`DebugUtils.h:14-39`), which is why it is deliberately not exposed through a public accessor.

A consequence given up: an allocation waiting in the retirement queue no longer carries a name of its own, so retirement debugging identifies parked storage by the buffer that released it rather than by the allocation.

### D5: The capability is exposed by `ComputeBuffer` only

The precondition for safe reallocation is a property of the *consumer*, not of the storage: the buffer's descriptor bindings must be re-established on every use. `ComputeBuffer` satisfies that (every dispatch rebinds by name, and `ShaderResourceBinding` re-hashes the bound handles when the set is requested). `IndexedBuffer` does not, for three independent reasons:

```
  CameraManager.cpp:111-126        its handle is written into descriptor sets once, at creation
  SceneDataManager.cpp:195-198     same pattern for the light UBO
  IndexedBuffer.cpp:26             it caches the mapped base pointer at construction
  (and its slice geometry depends on a fixed size)
```

So the mechanism is `protected` in `DeviceBuffer` and the public entry points live on `ComputeBuffer`, which makes the rule enforceable by the type system rather than by a comment. This also gives `ComputeBuffer` its first real contract: until now it was a creation-time tag with no state and no behaviour beyond a boolean-to-`BufferType` mapping.

### D6: The render side owns the model matrices buffer

With identity stable, ownership is decided by lifetime and ordering rather than by safety. The render side wins on both:

- **Same lifetime domain.** The render graph is a render-side object that holds a pointer to the buffer for its whole life. Putting the owner in the same subsystem (`RenderSystem`) removes the ordering question entirely. Physics ownership would require "the scene outlives the graph" as an app-level rule, and would require `PhysicsScene::Clear()` (which currently resets every GPU buffer, `PhysicsScene.cpp:135`) to be taught an exception.
- **The descriptor ordering becomes free.** `UploadSceneData` writes binding 2 during `StartFrame`. Because physics does not own the buffer, physics can never reallocate it, so the handle written there is final for the frame. Under physics ownership, any reallocation later in the frame (today impossible, but exactly what `physics-step-simplification` moves to record time) would leave the descriptor pointing at the previous storage.
- **Two conditionals disappear.** The buffer exists from `SceneDataManager::Create()`, so the graph imports it unconditionally and the editor's one-shot rebuild (D12) goes away.

**Alternatives considered:**

- *Physics keeps ownership, in place growth* — the smaller diff and free capacity control, but it keeps the graph pointing into another subsystem's object and makes `Clear()` a trap. Rejected.
- *The assembly layer owns it* — the honest "neither module owns the cross-module resource" answer, but it introduces a third owner concept for one buffer. Rejected.
- *Render derives the capacity from the largest `model_mat_index` it will read* (a tighter, purely render-side demand signal) — elegant, and the data is available (`World::UpdateRendererData` runs before the bridge). Rejected for now as a refinement: passing the physics slot count on the line that already exists is one argument and obviously correct.

### D7: Capacity is a demand passed once per frame

The render side exposes an element-count entry point and starts from an initial reservation. The assembly layer, on the line where the bridge already lives, reports the physics scene's rigid body slot count and then asks the physics side to produce. Two properties follow: the buffer is never smaller than what the producer will write, and the initial reservation (the current `MAX_MODEL_MATRICES` value, re-documented as an initial size rather than a cap) keeps the early frames from reallocating repeatedly.

Because the render side sizes it, an overflow is impossible by construction — the clamp in the solver entry point (D9) is defence against a future call site that forgets, not a live path.

### D8: The buffer is initialized to identity at creation

The failure mode of a missing production call is silent and already shipped once: `physicsapp-body-state-write` records that a zeroed buffer made every body invisible. The render side now creates the buffer, so it can write identity matrices into it once, at `Create()`, using the same submission helper and the same place that already clears the default shadow map (`SceneDataManager.cpp:192`). A missed production then renders a wrong transform instead of an invisible object.

This is a safety net, not a substitute for the frame rule (D10): the specs require production, and the tests assert it.

### D9: `ISolver::GPUCalcModelMatrices` is a caller-invoked entry point

```
  ISolver           virtual void GPUCalcModelMatrices(vk::CommandBuffer cb, Rhi::ComputeBuffer &target) = 0
  PhysicsSystem     void GPUCalcModelMatrices(PhysicsScene &scene, vk::CommandBuffer cb, Rhi::ComputeBuffer &target)
```

- **The target is a parameter, not scene state.** The physics side therefore never holds a reference to a render-owned object between calls, and the "nullable scene field" question does not arise.
- **It is scene-scoped at the `PhysicsSystem` level.** `GPUStep` iterates every scene's solvers; a target-less equivalent for model matrices would let two scenes write into one buffer. Today only the main scene has solvers registered (`MainClass.cpp:104`), so this is defence rather than a live bug — but it is exactly the assumption that should not be implicit.
- **The implementation records its own barrier.** It knows which earlier writes it reads, and every other compute pass in the repository does the same.
- **The caller ensures capacity; the implementation clamps.** Contract violation is a programming error, so it asserts in debug builds and clamps in release rather than corrupting memory or spamming a per-frame log.
- **Pure virtual, not a defaulted no-op.** Every concrete solver implements it, because there is deliberately no shared default implementation to fall back on: what "produce model matrices" means is solver-specific, and an interface-supplied no-op would let a solver silently render nothing. A solver that genuinely produces no matrices implements the entry point as an explicit no-op of its own. This also keeps the interface's definition in one place rather than splitting it across a translation unit that has to include Vulkan for the default body.

**Alternatives considered:** keeping the production inside `GPUStep` with a nullable target (the target would then have to be stored somewhere, and the physics app's seed would still need a way to run only that part) — rejected. Adding the hook to `PhysicsScene` instead of `ISolver` — the scene does not own solvers; `PhysicsSystem` does.

### D10: One frame rule replaces three accidental guarantees

> **Model matrices are produced once per frame, between the command buffer's `begin()` and the render graph's recording, independent of play, pause and simulation state.**

Per host:

```
  MainClass::RunOneFrame   GPUStep(cb) → produce → render_graph->RecordAllPasses(cb)
  editor example           the produce call sits OUTSIDE the m_is_playing gate,
                           at the same level as RecordAllPasses
  PhysicsApp               Step() produces after its GPUStep; CommitScene produces once
                           (the former seed pass, now the same call rather than a step)
```

Pause is covered without a special case: a paused frame recomputes from unchanged poses, so the result is identical while no longer depending on "nobody writes and nobody reads". The editor's `IsPhysicsActive` switch stops being load-bearing for buffer validity.

### D11: The model matrix shader becomes shared

`engine/Physics/shader/solver/XPBDSolver/model_matrix.comp` is not XPBD-specific: it reads `RigidBodyAlive`, `RigidBodyCenterPosition` and `RigidBodyCenterRotation` and writes `ModelMatrices`. The dummy solver's shader currently duplicates that mapping (plus a `quaternion_to_mat4` copy) inside its integration dispatch. Moving the file to `engine/Physics/shader/solver/common/` and using it from both solvers leaves exactly one place where a body pose becomes a model matrix, and it is what makes the dummy solver's split (D9) cheap: its `GPUStep` keeps only the displacement.

`physics-gpu-shaders`'s layout convention gains `common` as an allowed `<solver>` slot for a shader shared by more than one solver.

### D12: The editor's whole-graph rebuild disappears

The editor example currently builds its graph with no buffer and then rebuilds it wholesale once physics buffers appear, re-pointing both widget display textures (`main.cpp:261-292`, flag at `:204`). That rebuild exists only because the graph was built before the buffer existed. With the buffer created by the render system before any graph is built, the first build is final, and the flag, the rebuild, the texture re-pointing and the debug log are all deleted.

This is recorded as the general lesson: a build-time condition on a resource's availability does not merely risk a missing dependency edge, it forces the whole graph to be rebuilt.

### D13: The three in-flight changes are rebased in this change

Three unarchived changes restate vocabulary this change deletes, and their deltas would re-introduce it when they archive:

```
  compute-kernel-dispatch / descriptor-arena-epoch-buckets
      both carry the same MODIFIED `rhi-module` requirement body containing
      "…and the shared-ownership buffer factory" plus its scenario
  compute-kernel-dispatch / physics-step-simplification
      both list `solver/XPBDSolver/model_matrix.comp.spv` in `physics-gpu-shaders`
  physics-step-simplification
      owns `rhi-buffer-capacity` (which this change's `EnsureCapacity` implements),
      enumerates `ISolver`'s methods, and restates the model matrices forward
```

The alternative — landing after them — would leave the shared pointer in the code through two further changes and would have `physics-step-simplification` build record-time sizing on top of an ownership model that is about to be removed. The rebase is a task group in this change rather than a follow-up, so it cannot be forgotten.

## Risks / Trade-offs

- **[A caller forgets to produce model matrices]** → Mitigation: the buffer is initialized to identity (D8) so the failure is visible rather than invisible, the frame rule is a spec requirement (D10), and a paused-frame fixture asserts that physics-driven renderers keep valid matrices without a step.
- **[In-place reallocation invalidates a handle or mapped pointer someone cached]** → Mitigation: the capability is exposed only on the buffer type whose bindings are re-established per use (D5); the invalidation contract is explicit in the spec; a standalone RHI test asserts the handle changes while the object address does not.
- **[A reallocation frees storage an in-flight submission still reads]** → Not a new risk: the replaced allocation takes the existing retirement path, which the archived `gpu-buffer-retirement` change made the default for every allocation destroyed through the allocator.
- **[The render-side descriptor points at storage that was replaced later in the frame]** → Avoided by construction under render ownership (D6): physics cannot reallocate a buffer it does not own, so the handle written during `StartFrame` is final for the frame.
- **[PhysicsApp renders a stale pose after `SetBodyValue` while paused]** → Accepted and unchanged: the app's pause model defers writes to the next `Step()` (`RecordBodyStateUpload` is recorded there). The new entry point makes an immediate refresh a single call if that is ever wanted, which is recorded as a non-goal rather than silently changed.
- **[A scene that exceeds the initial reservation reallocates during a frame]** → Accepted: growth is in place, so the graph's reference and the object address are unaffected; only the handle changes, and the descriptor is rewritten every frame.
- **[The rebase edits three other changes' artifacts]** → Mitigation: the edits are mechanical text changes to unarchived, unimplemented changes, and they are an explicit task group with a validation step for each change.
- **[Spec scenario names retained although their premise is gone]** (see Context) → Accepted as a tooling constraint; the bodies are unambiguous and the constraint is recorded so a future change can rename the requirement properly.

## Migration Plan

1. Add the RHI capability: the protected storage-replacement primitive, the two `ComputeBuffer` entry points, the debug name remembered by `DeviceBuffer`, and the standalone test. No behaviour change on its own.
2. Convert the five `EnsureBuffer` copies to in-place replacement with exact-size semantics. Behaviour-neutral: only the object address stops changing.
3. Move model matrices ownership to `SceneDataManager`: ownership, demand sizing, identity initialization, unconditional import in both builders, and the deletion of the shared-ownership vocabulary and the builder parameters.
4. Add the solver entry point: `ISolver::GPUCalcModelMatrices`, the scene-scoped `PhysicsSystem` forwarding, the shared model matrix shader, and the two solver implementations.
5. Update the seven call sites, including the editor's rebuild removal and the physics app's commit-time production.
6. Rewrite the tests.
7. Rebase the three in-flight changes and re-validate each.

Rollback is per step. Steps 1–2 are additive and independently revertible. Step 3 is the one behavioural change; reverting it means restoring the forwarding call and the builder parameters, which the archived `gpu-buffer-retirement` artifacts still describe.

## Open Questions

- **Whether `IndexedBuffer` should ever gain a slice-growth entry point.** It is excluded by D5 for three reasons, one of which (the cached mapped pointer) would be trivial to fix and one of which (write-once descriptors) would not. Nothing needs it today.
- **Whether the capacity demand should be derived from the render side's largest model matrix index instead of the physics slot count.** A tighter bound and a purely render-side signal, deferred because the slot count is already on the line that does the wiring.
