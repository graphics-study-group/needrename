## 1. PhysicsScene API Additions

- [x] 1.1 Add `SetRigidBodyTransform(idx, pos, rot)` — updates rigid body world position and rotation post-registration
- [x] 1.2 Add `SetModelMatrixActive(idx, bool)` / `IsModelMatrixActive(idx)` — with `std::vector<bool> m_rigid_body_model_matrix_active`
- [x] 1.3 Modify `SetSimulationEnabled(false)` to clear all `m_rigid_body_model_matrix_active` entries
- [x] 1.4 Add `AllocateFixedJoint() → uint32_t` — allocates empty slot in fixed joints vector, returns index
- [x] 1.5 Add `UpdateFixedJoint(idx, obj1, obj2, compliance, rel_pos, rel_rot)` — fills allocated slot
- [x] 1.6 Add `AllocateHingeJoint() → uint32_t` — allocates empty slot in hinge joints vector, returns index
- [x] 1.7 Add `UpdateHingeJoint(idx, obj1, obj2, compliance, axis1, axis2, attach1, attach2)` — fills allocated slot

## 2. Physics Component Lifecycle Split

- [x] 2.1 RigidBodyComponent: Strip `EnqueueRigidBodyInitialization` and property upload from `Awake()`; keep `RegisterRigidBody` + `CollectShapesRecursively` + `SetCollisionShapeRigidBody`
- [x] 2.2 RigidBodyComponent: Implement `Init()` — `SetRigidBodyTransform` + `SetRigidBodyProperties` + `SetRigidBodyManualInertia` (if set) + `EnqueueRigidBodyInitialization` + `SetModelMatrixActive(true)`
- [x] 2.3 CollisionShapeComponent: Implement `Init()` — `UpdateCollisionShapeGeometry` + `TryAttachToAncestorRigidBody`
- [x] 2.4 PhysicsConstraintComponent: Strip handle resolution and joint registration from `Awake()`; keep `AllocateFixedJoint` / `AllocateHingeJoint` placeholder allocation
- [x] 2.5 PhysicsConstraintComponent: Implement `Init()` — resolve `ObjectHandle` → rigid body index, compute initial transforms, call `UpdateFixedJoint` / `UpdateHingeJoint`

## 3. Model Matrix SSBO Toggle in Rendering

- [x] 3.1 Modify `RendererComponent::PreRenderUpdate` to check `IsModelMatrixActive(rigid_idx)` before setting `model_mat_index = rigid_idx`
- [x] 3.2 When inactive, set `model_mat_index = -1` and use full world transform as model matrix

## 4. MainClass Physics Integration

- [x] 4.1 Add private `SetupDefaultPhysics()` method that creates `XpbdGpuSolver` with hardcoded config and registers it
- [x] 4.2 Call `SetupDefaultPhysics()` at the end of `LoadProject()`
- [x] 4.3 Add `InitializePendingRigidBodies` call in `RunOneFrame()` between `ProcessEvents` and `UpdateRendererData`

## 5. Editor Loop Restructuring

- [x] 5.1 Restructure editor main loop to match `RunOneFrame` structure: add `InitializePendingRigidBodies`, physics pipeline (gated on `m_is_playing`), manual CB management replacing `rg->Execute()`
- [x] 5.2 Add `#include <Physics/PhysicsSystem.h>` and required physics headers

## 6. EditorRenderGraphBuilder SSBO Support

- [x] 6.1 Add `const ComputeBuffer *model_matrices_buffer = nullptr` parameter to `BuildEditorRenderGraph`
- [x] 6.2 When non-null, import SSBO via `ImportExternalResource` with `ShaderRandomWrite` access
- [x] 6.3 Declare `UseBuffer(ShaderRandomRead)` in shadowmap pass, scene lit pass, and game lit pass when SSBO available
- [x] 6.4 Implement lazy render graph rebuild when SSBO transitions from null to non-null

## 7. Editor Start/Stop Callbacks

- [x] 7.1 Update `m_OnStart` callback to include `SetSimulationEnabled(true)` on main scene's physics scene
- [x] 7.2 Register `m_OnStop` callback with `SetSimulationEnabled(false)` + `SetModelMatrixActive` clearance

## 8. Verification

- [x] 8.1 Build and verify no compilation errors in engine, editor, and examples
- [ ] 8.2 Run `example/physics_example` — confirm physics still works after lifecycle split
- [ ] 8.3 Run `example/editor_run_game_example` — confirm objects render correctly in stopped state (TransformComponent path)
- [ ] 8.4 In editor, create scene with physics components (RigidBody + CollisionShape + StaticMesh), press Play — confirm physics simulation runs and objects move
- [ ] 8.5 In editor, press Stop — confirm simulation stops and objects freeze at last position
- [ ] 8.6 In editor, press Play again — confirm simulation resumes with re-initialized transforms
