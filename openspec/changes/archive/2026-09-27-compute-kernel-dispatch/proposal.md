## Why

Invoking one compute shader is a ten-step manual procedure spread across three RHI types. A caller must instantiate a `ComputeStage`, allocate a `ComputeResourceBinding` per stage, store both as members, write a byte-size for every buffer by hand, bind every interface by name through `GetShaderResourceBinding().BindBuffer(...)`, record push constants, and finally make three separate calls (`BindComputeStage` / `BindComputeResource` / `DispatchCompute`). Physics does this at 264 binding call sites across 42 `ComputeStage` instances, and the algorithms repeat the three-call sequence even internally.

The parts that could be automatic already exist and are simply not used:

- **The name→slot mapping is already reflected.** `SPLayout::Reflect` fills `SPInterface::layout_set` / `layout_binding` from the SPIR-V decorations and builds `interface_name_mapping` (`ShaderParameterLayout.cpp:411-437`). Nothing on the C++ side hardcodes a binding number; callers only ever write the GLSL block name (`srb.BindBuffer("RigidBodyAlive", ...)`).
- **Yet every dispatch re-derives it the hard way.** The bound names live in an ordered `std::map<std::string, InterfaceVariant>`, and every dispatch walks the whole reflected interface table to look its own names up there and to build the resolved-content vector the arena consumes — a string lookup per declared interface, for a dictionary that binds far fewer. The sibling `descriptor-arena-epoch-buckets` change (B, already landed) took the set cache, its content hash and its reclamation out of this class and into the arena; what this change removes is the remaining full-table traversal and lookup.
- **And nothing checks the names.** `ShaderResourceBinding::GetDescriptorSet` skips any declared interface with no bound name (`if (itr == pimpl->interfaces.end()) { continue; }` — `ShaderResourceBinding.cpp:68-71`, the ordered map at `:28`), leaving an unwritten descriptor. A renamed GLSL block or a typo'd binding string fails silently.

Three further costs come from the same stack: the same shader is loaded independently by two components (`clear_int_buffer.comp` by both `XPBDGpuSolver` and `ConvexCollisionDetector`), so one pipeline exists per user; nine near-identical `LoadPhysicsSpirv*`-style helpers are copy-pasted — eight across `engine/Physics/` and one in a headless test; and the descriptor sets are already arena-owned, so what remains duplicated is pipeline creation and shader-module loading.

This change gives compute shaders a single call surface — a kernel invoked like a function with a name→resource dictionary and a grid — and deletes the three-layer type stack behind it.

## What Changes

- **New RHI facility: `ComputeKernel`.** A kernel is identified by a caller-supplied module identity — a shader asset's GUID, or a directly loaded module's path — loaded lazily, and cached per device. One pipeline exists per identity no matter how many components use it, and a request that hits neither reads nor scans the module.
- **Dictionary dispatch.** `Dispatch(cb, {name → resource}, grid, push)` replaces the four-call sequence. Names are resolved against the reflected interface table **once, at kernel load**, into a dense table; at dispatch each supplied name is matched against that table once, and the arena — not the kernel — keys the resulting set on the resolved `(binding number, handle, offset, size)` entries. The win is the call surface and the pipeline/shader-module dedup rather than the hot path: the per-dispatch arena acquisition is change B's contract and stays.
- **Binding entries are a variant: buffer or texture (+ subresource range).** Texture binding is required because every bloom pass binds `inputImage` / `outputImage` through `GetShaderResourceBinding().BindTexture(...)` (`ComplexRenderGraphBuilder.cpp:214-219`, the editor's two passes at `EditorRenderGraphBuilder.cpp:233-241` and `:315-323`, and `pbr_test`'s copy), and all of them migrate in this change.
- **(BREAKING) Name validation throws `std::runtime_error`** when the dictionary names an interface the shader does not declare, or omits one it does. The current silent skip is removed.
- **No implicit barriers.** `Dispatch` inserts no barrier. Barrier placement stays manual and unchanged — an explicit decision, recorded as a responsibility table in `design.md`.
- **Kernel keeps `ComputeStage`'s no-Asset property:** constructed from SPIR-V words (no `ShaderAsset`), and it declares its push-constant range from the reflected `push_constant_size`.
- **Descriptor sets are obtained from change B's descriptor arena on every dispatch**, which is what satisfies that arena's re-acquisition contract. There is no `slot_count` and no caller-supplied rotation parameter: a set that is re-acquired each epoch and reclaimed only under cache pressure needs no rotation.
- **One SPIR-V loader, and one identity wrapper per shader mode.** The eight component-local `LoadPhysicsSpirv*` copies and a ninth in a headless test collapse into one shared helper in `engine/Physics/` — it cannot live in `Rhi`, because it resolves a path against `ENGINE_PHYSICS_SPIRV_DIR` — preserving that contract and its failure contract (throw with the absolute path attempted). `LoadPhysicsKernel` sits on top of it and looks the module up by its path first, so a component that shares a module with another pays no file read at all; the shader-asset mode gets the matching wrapper in `Render` (`ShaderKernel.h`), keyed by the asset's GUID. Both modes therefore reach the cache through an identity they already own, and no call site invents one.
- **Migrations:** `RadixSort`, `SumByKey`, `ParallelScan`, `CompactUnique`, `SpatialHashBroadDetector`, `ConvexCollisionDetector`, `XPBDGpuSolver`, `DummySolver`, and every bloom pass (the framework builder's, the editor's two, and `pbr_test`'s copy).
- **(BREAKING) Deletions after migration:** `ComputeStage`, `ComputeResourceBinding`, `ComputeHelpers`, the three `CommandBuffer` compute wrappers that take those types, and `SPLayout`'s now-unused descriptor-pool-facing entry points if any remain. `ShaderResourceBinding`, `IndexedBuffer`, `StructuredBuffer` and `StructuredBufferPlacer` are **kept** — the material path depends on them and they are out of scope. One piece of `ShaderResourceBinding` is factored out rather than duplicated: its interface→`ResolvedBinding` mapping becomes a shared resolver the kernel also calls, with its public contract unchanged.
- **Behavior is frozen otherwise.** Grow-only resizing, push-constant counts, and the deletion of `PreGPUStep` / `PostGPUStep` including detector `Configure` folding all belong to the later `physics-step-simplification` change.

## Capabilities

### New Capabilities

- `rhi-compute-kernel`: the kernel identity and device-level caching contract, the name→resource dictionary dispatch, name validation, texture binding, the reflected push-constant contract, and the no-implicit-barrier rule. Carries forward the surviving constraint from `rhi-compute-resource-binding` that the public API and its documentation contain no render-frame vocabulary, and from `rhi-module` that the facility has no Asset dependency.

### Modified Capabilities

- `rhi-compute-resource-binding`: **removed** — all four requirements describe `ComputeResourceBinding` and `ComputeStage`, which this change deletes. The no-frame-vocabulary constraint migrates to `rhi-compute-kernel`.
- `rhi-module`: the type inventory drops the three deleted types and gains the kernel; the "`ComputeStage` has no Asset dependency" requirement is removed, with its property re-stated on the kernel in `rhi-compute-kernel`.
- `rhi-descriptor-arena`: the layout-sharing requirement's second scenario names the deleted `ComputeStage`; it is re-expressed on the kernel, which is the compute pipeline object from this change on.
- `rhi-directory-structure`: the `Pipeline/` source inventory drops the deleted files and gains the kernel's.
- `physics-gpu-shaders`: the two requirements that say loaded SPIR-V is handed to `ComputeStage::Instantiate` and that `ComputeStage` instances are created per shader are re-expressed on the kernel.
- `rhi-push-constants`: the push-constant range declaration and the "record `sizeof(T)` at offset 0, assert it fits" contract move from `ComputeStage` / `PushConstants` to the kernel and its dispatch.
- `physics-push-constants`: the "single rotation slot" requirement loses the APIs it names (`AllocateResourceBinding` / `Rhi::BindComputeResource`); its protective intent — no rotation depth and no frame counter in physics — is re-stated without them.
- `gpu-radix-sort`, `gpu-sum-by-key`: the class-construction requirement's dependency list names the deleted types; it is re-expressed in terms of the kernel.
- `detector-configure-detect`: `Record`'s dispatch mechanism and the binding-allocation requirement name the deleted types and the `CommandBuffer` wrappers.
- `physics-dummy-solver`: the `GPUStep` dispatch requirement names the deleted `CommandBuffer` wrappers.
- `rhi-structured-buffer-placer`: the UBO-slice-sizing requirement names `ComputeResourceBinding`; only the material half survives.

## Impact

**Code**
- `engine/Rhi/Pipeline/` — new `ComputeKernel`; deleted `ComputeStage`, `ComputeResourceBinding`, `ComputeHelpers`; one shared interface→`ResolvedBinding` resolver used by the kernel and `ShaderResourceBinding`.
- `engine/Physics/` — the shared SPIR-V loader helper, and `LoadPhysicsKernel` on top of it. It cannot live in `Rhi`: it resolves a path against `ENGINE_PHYSICS_SPIRV_DIR`.
- `engine/Render/Asset/Shader/ShaderKernel.h` — new: the shader-asset identity wrapper, the asset-mode half of the same lookup.
- `engine/Physics/gpu_algorithm/{RadixSort,SumByKey,ParallelScan,CompactUnique}` — dispatch call sites and dependency declarations.
- `engine/Physics/Collision/{SpatialHashBroadDetector,ConvexCollisionDetector}` — dispatch call sites.
- `engine/Physics/Solver/{XPBDGpuSolver,DummySolver}` — the 132 binding call sites in `XPBDGpuSolver.cpp` alone collapse into dictionary dispatches.
- `engine/Render/Pipeline/CommandBuffer.*` — the three compute wrappers and `m_bound_compute_stage` are deleted.
- `engine/Render/FullRenderSystem.h` — the two includes of the deleted headers are dropped.
- `engine/Framework/Tools/ComplexRenderGraphBuilder.*` — the bloom pass migrates to the kernel API.
- `app/editor/Editor/Render/EditorRenderGraphBuilder.*` — the editor's two bloom passes (scene and game widget) migrate the same way.
- `test/engine/headless/{compute_buffer_test,headless_compute_test,gpu_compact_unique_test}.cpp` and `test/engine/windowed/{compute_shader_test,pbr_test,new_material_test}.cpp` — every test that constructs a compute stage migrates; `pbr_test` carries a third copy of the bloom pass.

**API**
- Breaking for every compute dispatch call site (264 binding call sites measured in `engine/Physics/`, plus ~35 dispatch sites). No public API outside `Rhi` changes shape except the removal of the `CommandBuffer` compute wrappers.

**Spec deltas deliberately deferred**
- Four capabilities still name the deleted types but are stale for unrelated reasons and are owned by other work: `gpu-parallel-scan` (names `RenderSystem` and `RenderGraphBuilder`), `gpu-convex-collision-detection` and `spatial-hash-broad-phase` ("self-owned RenderGraph recording"), and `xpbd-solver-multi-rg` (describes `PreGPUStep` responsibilities and a no-allocation rule that `physics-step-simplification` rewrites). Rewriting them here would cement text another change already owns. `design.md` records the exact list and adds the sweep to `physics-step-simplification`. The descriptor arena's compute-pipeline scenario named a deleted type too; it is **included** in this change's delta set rather than deferred, because that block is otherwise current.

**Tests**
- Each migrated algorithm keeps its existing headless fixture and must pass unchanged; the new name-validation behavior needs its own unit test (declared-but-unbound and bound-but-not-declared both throw, naming the shader and the offending interface).
- The bloom pass needs a windowed or headless visual/render check after migration, in all three places it exists (`ComplexRenderGraphBuilder`, the editor's two passes, and `pbr_test`).
- The shared loader is covered by a unit test for its success path and both failure branches, against a present and a missing module.
