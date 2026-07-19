## Why

`PhysicsScene` has accumulated responsibilities spanning both GO space (GameObject handles, COM computation from shape geometry, GO→COM coordinate conversions) and COM space (SoA storage, GPU buffer sync). This blurs the boundary between the Framework and Physics modules, forces `engine/Physics/` to include Framework headers (`GameObject.h`, `Scene.h`, component types), and makes the PhysicsScene interface shallow — 12-parameter registration methods that expose every internal SoA column. Adding a single rigid body property requires touching 7+ locations across both modules.

The goal is a clean architectural split: Framework-layer code handles all GO-space concerns; Physics-layer code operates purely in COM space with no knowledge of GameObjects.

## What Changes

- **Introduce `PhysicsAdaptor`** in `Framework/world/physics/` as the sole bridge between GO and COM spaces. Components submit GO-space Descriptors to the Adaptor during Init. The Adaptor computes COM, inertia, and conversions during Flush, then submits COM-space Descriptors to PhysicsScene.
- **Simplify `PhysicsScene` interface**: replace `RegisterRigidBody(12 params)`, `SetRigidBodyProperties(9 params)`, `SetRigidBodyTransform`, `RecalculateRigidBodyState`, `InitializePendingRigidBodies`, `ResolveCollisionFilters`, and all pending queues with unified `AllocateSlot` + `Submit*(DESCRIPTOR)` + `SyncGpuBuffers`.
- **Remove GameObject knowledge from `Physics/`**: eliminate all `#include` of Framework headers from the Physics module. Four reverse includes (`Handle.h`, `GameObject.h`, `Scene.h`, `CollisionShapeComponent.h`) will be removed.
- **BREAKING**: `PhysicsScene::RegisterRigidBody`, `SetRigidBodyProperties`, `SetRigidBodyTransform`, `EnqueueRigidBodyInitialization`, `InitializePendingRigidBodies`, `SetModelMatrixActive`, `IsModelMatrixActive`, `ResolveCollisionFilters`, `FindRigidBodyByObjectHandle` are removed. Callers must use PhysicsAdaptor.
- **BREAKING**: Collision shape `world_position` / `world_rotation` columns removed from PhysicsScene SoA and GPU buffers. Broad-phase collision detection must recompute world AABBs from COM pose + shape local pose (shader change required).
- **BREAKING**: `BindShapeToRigidBody` interface removed. RB↔Shape binding is declared via `CollisionShapeComDescriptor::bound_rigid_body`.
- **Model matrix**: Adaptor owns activation state (`IsPhysicsActive`). Solver still generates COM→GO model matrix SSBO. RendererComponent queries Adaptor for COM offset, computes its own offset matrix, and submits offset + `model_mat_index` to the rendering system.
- **Descriptor structs**: Define `RigidBodyDescriptor`, `CollisionShapeDescriptor`, `JointSubmitData` (GO-space, component→Adaptor) and `RigidBodyComDescriptor`, `CollisionShapeComDescriptor` (COM-space, Adaptor→PhysicsScene).
- **Internal modules**: `ComInertiaComputer` and `JointConverter` as pure-function modules in `Framework/world/physics/Internal/`.
- **Scene integration**: `Scene` gains `GetPhysicsAdaptor()`, `FlushPhysics(RenderSystem&)`. Main loop calls `FlushPhysics` after `ProcessEvents`.

## Capabilities

### New Capabilities
- `physics-adaptor`: The PhysicsAdaptor class owned by Scene — handle mappings, pending descriptor storage, COM offset cache, model matrix activation state, and the public interface used by components, renderers, and Scene.
- `go-descriptors`: GO-space descriptor structs (`RigidBodyDescriptor`, `CollisionShapeDescriptor`, `JointSubmitData`) built by components during Init and submitted to Adaptor.
- `com-descriptors`: COM-space descriptor structs (`RigidBodyComDescriptor`, `CollisionShapeComDescriptor`) built by Adaptor during Flush and submitted to PhysicsScene.
- `physics-adaptor-flush`: The Flush pipeline — collision filter resolution, COM/inertia computation, GO→COM joint conversion, COM descriptor building, and PhysicsScene sync.

### Modified Capabilities
- `collision-filtering`: `PhysicsScene::ResolveCollisionFilters` is replaced by Adaptor::Flush performing the same resolution. Filter data is embedded in `CollisionShapeComDescriptor::ignore_shape_indices` rather than stored as pending ObjectHandle arrays in PhysicsScene.
- `renderer-ancestor-rigidbody-attach`: RendererComponent queries `PhysicsAdaptor::FindRigidBodyByObjectHandle()` instead of `PhysicsScene::FindRigidBodyByObjectHandle()` to walk the ancestor chain. The local transform computation formula (`inverse(rb_go_world) * renderer_world`) is replaced by offset computation using Adaptor's `GetComOffsetLocal`.
- `physics-scene-builder`: Example `SceneBuilder` methods now call Adaptor for slot allocation and use the reduced PhysicsScene interface. Component friction/restitution/mass settings remain unchanged at the component level.

## Impact

- `engine/Framework/world/physics/` — new directory with PhysicsAdaptor, PhysicsDescriptors, ComInertiaComputer, JointConverter
- `engine/Framework/world/Scene.h/.cpp` — add `GetPhysicsAdaptor()`, `FlushPhysics()`
- `engine/Framework/component/physics/RigidBodyComponent.h/.cpp` — rewrite Awake/Init to use Adaptor
- `engine/Framework/component/physics/CollisionShapeComponent.h/.cpp` — rewrite Awake/Init, eliminate duplicated cylinder fallback
- `engine/Framework/component/physics/PhysicsConstraintComponent.h/.cpp` — rewrite Awake/Init
- `engine/Framework/component/RendererComponent.cpp` — query Adaptor instead of PhysicsScene, compute offset matrix
- `engine/Physics/PhysicsScene.h/.cpp` — remove ~16 methods, ~10 data members, 4 Framework #includes. Add Submit* and Allocate*Slot methods.
- `engine/Physics/PhysicsSystem.h/.cpp` — remove `InitializePendingRigidBodies`; minor cleanup
- `engine/Physics/Solver/XPBDGpuSolver.cpp` — remove references to removed PhysicsScene members; verify COM-space data consumption unchanged
- `engine/MainClass.cpp` — replace `physics->InitializePendingRigidBodies()` with `scene.FlushPhysics()`
- `example/editor_run_game_example/main.cpp` — same replacement
- `example/physics_example/` — update calls to use new Scene/Adaptor interface
- `docs/adr/0004-physics-adaptor-separation.md` — reference document (already written)
- `CONTEXT.md` — updated (already done)
