## Context

`PhysicsScene` currently interleaves GO-space and COM-space logic in a single 1329-line class. Components call it directly via `GetScene()->GetPhysicsScene()`, passing GO-world coordinates and component properties through multi-parameter methods. The class internally computes COM, inertia, and GO→COM conversions, then uploads results to GPU buffers.

The engine architecture has a clear Framework/Physics module boundary, but four reverse `#include`s violate it (`GameObject.h`, `Scene.h`, `CollisionShapeComponent.h`, `Handle.h` from `Physics/` into `Framework/`). Full details in [ADR-0004](../../docs/adr/0004-physics-adaptor-separation.md).

## Goals / Non-Goals

**Goals:**
- Separate GO-space logic (components, handles, transforms, COM computation, GO→COM conversion) into `Framework/world/physics/PhysicsAdaptor`
- Reduce `PhysicsScene` to pure COM-space storage with no Framework dependencies
- Provide clean Descriptor-based interfaces between components→Adaptor (GO space) and Adaptor→PhysicsScene (COM space)
- Enable independent testing of COM computation and joint conversion as pure functions
- Maintain backward compatibility for solver and rendering code (same GPU buffer interface)

**Non-Goals:**
- Change the XPBD solver or collision detection algorithms
- Add dirty tracking for GPU buffer uploads (still full upload per sync)
- Add runtime (hot-add/remove) physics object support
- Add GPU→CPU readback of simulation results
- Change the reflection/serialization system for physics types

## Decisions

### D-1: Adaptor owns all GO-space state; PhysicsScene owns only COM-space SoA

Adaptor maintains ObjectHandle↔index mappings, pending descriptor storage, COM offset cache, model matrix activation flags. PhysicsScene maintains SoA arrays (15 rigid body columns + 6 shape columns), slot alive flags, `shape_idx→rb_idx` vector, joint definitions, GPU buffers. See ADR-0004 §D-4 for the full list of removals from PhysicsScene.

### D-2: Two-layer Descriptor architecture

GO-space Descriptors carry component field values + GO world transforms. COM-space Descriptors carry computed values (COM pos/rot/offset, inertia, shape local poses). This decouples the component API (what users edit) from the physics API (what solvers consume). Structs are defined in `PhysicsDescriptors.h`.

### D-3: Flush as the single sync point

All Init data is buffered in `unordered_map<uint32_t, T>` pending maps. `Scene::FlushPhysics(RenderSystem&)` processes them in 5 steps: resolve filters → compute COM+inertia → submit shape COM descriptors → convert joints → sync GPU. No incremental updates during Init. This keeps the Adaptor's state machine simple: accumulate → process → clear.

### D-4: ComInertiaComputer and JointConverter as pure functions

Neither module holds internal state. They take inputs, return outputs. This makes them trivially testable — feed synthetic shape data and verify COM position / inertia tensor / joint COM-local coordinates. Located in `Internal/` subdirectory with `.hpp` extension to signal they are not part of the public interface.

### D-5: Model matrix: Solver owns GPU; Adaptor owns CPU activation state

The solver continues to run `model_matrix.comp` and write GO-world mat4 to an SSBO (this belongs to PhysicsScene's GpuMirror). Adaptor owns `IsPhysicsActive()` and `SetPhysicsActive(bool)`, set by editor Play/Stop. RendererComponent queries Adaptor for COM offset and computes its own local-to-COM offset matrix, submitted alongside `model_mat_index` to the rendering system's vertex shader composition (`model_matrices[idx] * offset`).

See ADR-0004 §D-9 for the dual-switch design (Adaptor's IsPhysicsActive vs PhysicsScene's SetSimulationEnabled).

### D-6: RB↔Shape binding during Init (not Awake)

Both `RigidBodyComponent::CollectShapesRecursivelyAndBind` and `CollisionShapeComponent::TryAttachToAncestorRigidBody` are called during Init. Whichever runs second completes the binding via `Adaptor::BindShapeToRigidBody`. Awake only allocates slots — no binding, no property submission. This simplifies the lifecycle: Awake = reserve, Init = configure + bind.

### D-7: File conventions

Public headers (Adaptor interface, Descriptor structs) use `.h`. Internal implementation modules use `.hpp` under `Internal/`. Physics/ module must not `#include` any Framework headers.

## Risks / Trade-offs

**[Risk] Collision shape world_pos/rot removal requires shader changes**
The `SpatialHashBroadDetector` currently reads `shape_world_position`/`shape_world_rotation` from GPU buffers to compute AABBs. After removal, it must compute world pose from `COM pose + shape local pose`. This is a GPU-side change but the math is straightforward (one quaternion rotation + vector addition per shape). Mitigation: the shader change is localized to `compute_cell_aabbs.comp` and `generate_broad_pairs.comp`.

**[Risk] Component→PhysicsScene→Solver call chain refactoring is wide**
The change touches `RigidBodyComponent`, `CollisionShapeComponent`, `PhysicsConstraintComponent`, `Scene`, `PhysicsScene`, `PhysicsSystem`, `MainClass`, and example code. Mitigation: implement in the task order specified below, where Adaptor and Descriptors are built first, then PhysicsScene is simplified, then components are migrated.

**[Trade-off] Adaptor duplicates topology knowledge already in PhysicsScene**
Both Adaptor and PhysicsScene need to know which shapes belong to which rigid body (Adaptor for COM computation, PhysicsScene for solvers). This is inherent to the split — Adaptor needs it during Flush, PhysicsScene needs it during simulation. Mitigation: the data volume is small (index arrays), and the binding is established once during Init then mirrored to both sides.

**[Trade-off] Flush is a synchronous CPU-side barrier**
All pending COM computation and joint conversion runs synchronously in `FlushPhysics`, before any GPU work begins. For large scenes this could be a frame-time concern. Mitigation: current scenes are small (dozens of bodies). If scaling becomes an issue, COM computation can be parallelized (independent per rigid body) and joint conversion can be deferred further. Not in scope for this change.
