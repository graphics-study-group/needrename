## Why

Invoking one compute shader is a ten-step manual procedure spread across three RHI types. A caller must instantiate a `ComputeStage`, allocate a `ComputeResourceBinding` per stage, store both as members, write a byte-size for every buffer by hand, bind every interface by name through `GetShaderResourceBinding().BindBuffer(...)`, record push constants, and finally make three separate calls (`BindComputeStage` / `BindComputeResource` / `DispatchCompute`). Physics does this at 261 binding call sites across ~40 `ComputeStage` instances, and the algorithms repeat the three-call sequence even internally.

The parts that could be automatic already exist and are simply not used:

- **The name→slot mapping is already reflected.** `SPLayout::Reflect` fills `SPInterface::layout_set` / `layout_binding` from the SPIR-V decorations and builds `interface_name_mapping` (`ShaderParameterLayout.cpp:411-437`). Nothing on the C++ side hardcodes a binding number; callers only ever write the GLSL block name (`srb.BindBuffer("RigidBodyAlive", ...)`).
- **Yet every dispatch re-derives it the hard way.** The bound names live in a `std::map<std::string, InterfaceVariant>`, and the descriptor-set cache key is a hash over all of them, so each dispatch re-hashes 7–20 `std::string` keys through an ordered map.
- **And nothing checks the names.** `ShaderResourceBinding::GetDescriptorSet` skips any declared interface with no bound name (`if (itr == intfc.end()) continue;` — `ShaderResourceBinding.cpp:117-120`), leaving an unwritten descriptor. A renamed GLSL block or a typo'd binding string fails silently.

Three further costs come from the same stack: the same shader is loaded independently by two components (`clear_int_buffer.comp` by both `XpbdGpuSolver` and `ConvexCollisionDetector`), so one pipeline and one descriptor pool exist per user; eight near-identical `LoadPhysicsSpirv*` helpers are copy-pasted across `engine/Physics/`; and descriptor sets are interned forever, which is the sibling problem change B (`descriptor-arena-epoch-buckets`) exists to fix.

This change gives compute shaders a single call surface — a kernel invoked like a function with a name→resource dictionary and a grid — and deletes the three-layer type stack behind it.

## What Changes

- **New RHI facility: `ComputeKernel`.** A kernel is identified by its SPIR-V path, loaded lazily, and cached per device. One pipeline exists per shader no matter how many components use it.
- **Dictionary dispatch.** `Dispatch(cb, {name → resource}, grid, push)` replaces the four-call sequence. Names are resolved against the reflected interface table **once, at kernel load**, into a dense table; at dispatch only `(binding number, handle, offset, size)` is hashed.
- **Binding entries are a variant: buffer or texture (+ subresource range).** Texture binding is required because the bloom pass binds `inputImage` / `outputImage` through `GetShaderResourceBinding().BindTexture(...)` (`ComplexRenderGraphBuilder.cpp:222-227`) and migrates in this change too.
- **(BREAKING) Name validation throws `std::runtime_error`** when the dictionary names an interface the shader does not declare, or omits one it does. The current silent skip is removed.
- **No implicit barriers.** `Dispatch` inserts no barrier. Barrier placement stays manual and unchanged — an explicit decision, recorded as a responsibility table in `design.md`.
- **Kernel keeps `ComputeStage`'s no-Asset property:** constructed from SPIR-V words (no `ShaderAsset`), and it declares its push-constant range from the reflected `push_constant_size`.
- **Descriptor sets are obtained from change B's descriptor arena on every dispatch**, which is what satisfies that arena's re-acquisition contract. There is no `slot_count` and no caller-supplied rotation parameter: a set that is re-acquired each epoch and reclaimed only under cache pressure needs no rotation.
- **One SPIR-V loader.** The eight `LoadPhysicsSpirv*` copies collapse into one shared helper preserving the runtime `ENGINE_PHYSICS_SPIRV_DIR` contract and its failure contract (throw with the absolute path attempted).
- **Migrations:** `RadixSort`, `SumByKey`, `ParallelScan`, `CompactUnique`, `SpatialHashBroadDetector`, `ConvexCollisionDetector`, `XpbdGpuSolver`, `DummySolver`, and the bloom pass.
- **(BREAKING) Deletions after migration:** `ComputeStage`, `ComputeResourceBinding`, `ComputeHelpers`, the three `CommandBuffer` compute wrappers that take those types, and `SPLayout`'s now-unused descriptor-pool-facing entry points if any remain. `ShaderResourceBinding`, `IndexedBuffer`, `StructuredBuffer` and `StructuredBufferPlacer` are **kept** — the material path depends on them and they are out of scope.
- **Behavior is frozen otherwise.** Grow-only resizing, push-constant counts, and the deletion of `PreGPUStep` / `PostGPUStep` including detector `Configure` folding all belong to the later `physics-step-simplification` change.

## Capabilities

### New Capabilities

- `rhi-compute-kernel`: the kernel identity and device-level caching contract, the name→resource dictionary dispatch, name validation, texture binding, the reflected push-constant contract, and the no-implicit-barrier rule. Carries forward the surviving constraint from `rhi-compute-resource-binding` that the public API and its documentation contain no render-frame vocabulary, and from `rhi-module` that the facility has no Asset dependency.

### Modified Capabilities

- `rhi-compute-resource-binding`: **removed** — all four requirements describe `ComputeResourceBinding` and `ComputeStage`, which this change deletes. The no-frame-vocabulary constraint migrates to `rhi-compute-kernel`.
- `rhi-module`: the type inventory drops the three deleted types and gains the kernel; the "`ComputeStage` has no Asset dependency" requirement is removed, with its property re-stated on the kernel in `rhi-compute-kernel`.
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
- `engine/Rhi/Pipeline/` — new `ComputeKernel`; deleted `ComputeStage`, `ComputeResourceBinding`, `ComputeHelpers`.
- `engine/Rhi/Buffer/` — shared SPIR-V loader helper.
- `engine/Physics/gpu_algorithm/{RadixSort,SumByKey,ParallelScan,CompactUnique}` — dispatch call sites and dependency declarations.
- `engine/Physics/Collision/{SpatialHashBroadDetector,ConvexCollisionDetector}` — dispatch call sites.
- `engine/Physics/Solver/{XPBDGpuSolver,DummySolver}` — the ~141 binding call sites in `XPBDGpuSolver.cpp` collapse into dictionary dispatches.
- `engine/Render/Pipeline/CommandBuffer.*` — the three compute wrappers and `m_bound_compute_stage` are deleted.
- `engine/Framework/Tools/ComplexRenderGraphBuilder.cpp` — the bloom pass migrates to the kernel API.

**API**
- Breaking for every compute dispatch call site (~261 binding sites plus ~35 dispatch sites). No public API outside `Rhi` changes shape except the removal of the `CommandBuffer` compute wrappers.

**Spec deltas deliberately deferred**
- Four capabilities still name the deleted types but are stale for unrelated reasons and are owned by other work: `gpu-parallel-scan` (names `RenderSystem` and `RenderGraphBuilder`), `gpu-convex-collision-detection` and `spatial-hash-broad-phase` ("self-owned RenderGraph recording"), and `xpbd-solver-multi-rg` (describes `PreGPUStep` responsibilities and a no-allocation rule that `physics-step-simplification` rewrites). Rewriting them here would cement text another change already owns. `design.md` records the exact list and adds the sweep to `physics-step-simplification`.

**Tests**
- Each migrated algorithm keeps its existing headless fixture and must pass unchanged; the new name-validation behavior needs its own unit test (declared-but-unbound and bound-but-not-declared both throw, naming the shader and the offending interface).
- The bloom pass needs a windowed or headless visual/render check after migration.
