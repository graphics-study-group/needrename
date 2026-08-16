## 1. Descriptor headers and internal modules

- [x] 1.1 Create `engine/Framework/world/physics/PhysicsDescriptors.h` with all Descriptor structs (RigidBodyDescriptor, CollisionShapeDescriptor, FixedJointSubmitData, HingeJointSubmitData, JointSubmitData variant, RigidBodyComDescriptor, CollisionShapeComDescriptor)
- [x] 1.2 Create `engine/Framework/world/physics/Internal/ComInertiaComputer.hpp` — pure function: `Compute(const RigidBodyDescriptor&, const vector<ShapeComputationData>&)` → Output (COM pos/rot/offset, inertia, shape poses)
- [x] 1.3 Create `engine/Framework/world/physics/Internal/JointConverter.hpp` — pure functions: `ConvertFixed(const FixedJointSubmitData&, c1, c2) → GpuFixedJoint`, `ConvertHinge(const HingeJointSubmitData&, c1, c2) → GpuHingeJoint`
- [x] 1.4 Verify: `ComInertiaComputer` and `JointConverter` do not include any Framework headers beyond math types

## 2. PhysicsScene interface simplification

- [ ] 2.1 Add `AllocateRigidBodySlot()`, `AllocateCollisionShapeSlot()`, `AllocateFixedJoint()`, `AllocateHingeJoint()` to PhysicsScene — allocate slot, mark alive=1, zero-initialize columns, return index
- [ ] 2.2 Add `SubmitRigidBody(uint32_t, const RigidBodyComDescriptor&)`, `SubmitCollisionShape(uint32_t, const CollisionShapeComDescriptor&)`, `SubmitFixedJoint(uint32_t, const GpuFixedJoint&)`, `SubmitHingeJoint(uint32_t, const GpuHingeJoint&)` — write descriptor fields to SoA columns
- [ ] 2.3 Add `UnregisterFixedJoint(uint32_t)`, `UnregisterHingeJoint(uint32_t)` — set alive=0 (match existing Unregister pattern for rigid bodies and shapes)
- [ ] 2.4 Rename `RefreshGpuBuffers` → `SyncGpuBuffers` (public), keep existing logic
- [ ] 2.5 Remove from PhysicsScene header and impl: `RegisterRigidBody`, `SetRigidBodyProperties`, `SetRigidBodyTransform`, `SetRigidBodyManualInertia`, `SetRigidBodyManualCenterOfMass`, `EnqueueRigidBodyInitialization`, `RecalculateRigidBodyState`, `InitializePendingRigidBodies`, `ConvertPendingJointUpdates`, `ResolveCollisionFilters`, `SetModelMatrixActive`, `IsModelMatrixActive`, `SetCollisionShapeRigidBody`, `FindRigidBodyByObjectHandle`
- [ ] 2.6 Remove from PhysicsScene private members: `m_rigid_body_to_object`, `m_object_to_rigid_body`, `m_need_init`, `m_init_queue`, `m_use_manual_inertia_com`, `m_manual_inertia`, `m_manual_center_of_mass`, `m_model_matrix_active`, `m_pending_filter_handles`, `m_shape_filter_offset`, `m_shape_filter_count`, `m_shape_filter_data`, `m_shape_world_position`, `m_shape_world_rotation`, `m_gpu_rigid_body_shape_offset`, `m_gpu_rigid_body_shape_count`, `m_gpu_flattened_shape_indices`, `PendingFixedJointUpdate`, `PendingHingeJointUpdate`, pending joint vectors
- [ ] 2.7 Remove Framework #includes from PhysicsScene.h and PhysicsScene.cpp: `Handle.h`, `GameObject.h`, `Scene.h`, `CollisionShapeComponent.h`
- [ ] 2.8 Remove `FindRigidBodyByObjectHandle`, `FindShapeByComponentHandle` from PhysicsScene
- [ ] 2.9 Update `PhysicsGpuBuffers` struct: remove `shape_world_position`, `shape_world_rotation`, `rigid_body_shape_offset`, `rigid_body_shape_count`, `flattened_shape_indices` fields
- [ ] 2.10 Compile and verify Physics/ module builds without Framework headers

## 3. PhysicsAdaptor implementation

- [ ] 3.1 Create `engine/Framework/world/physics/PhysicsAdaptor.h` — declare class with public interface: `AllocateSlot(handle)`, `AllocateShapeSlot(handle)`, `AllocateFixedJoint()`, `AllocateHingeJoint()`, `SubmitRigidBody`, `SubmitShape`, `SubmitJoint`, `BindShapeToRigidBody`, `Flush(RenderSystem&)`, `FindRigidBodyByObjectHandle`, `GetComOffsetLocal`, `IsPhysicsActive`, `SetPhysicsActive`, `GetPhysicsScene`
- [ ] 3.2 Implement idempotent slot allocation with internal `unordered_map<ObjectHandle, uint32_t>` and `unordered_map<ComponentHandle, uint32_t>` mappings
- [ ] 3.3 Implement pending storage: `unordered_map<uint32_t, RigidBodyDescriptor>`, `unordered_map<uint32_t, CollisionShapeDescriptor>`, `unordered_map<uint32_t, JointSubmitData>`
- [ ] 3.4 Implement `Flush` pipeline: resolve filters → compute COM+inertia for all pending bodies → submit shape COM descriptors → convert joints → submit joint COM descriptors → clear pending maps → call `m_physics_scene.SyncGpuBuffers(render_system)`
- [ ] 3.5 Implement collision filter resolution in Flush: ObjectHandle → shape index via Scene + Adaptor mappings, enforce symmetry
- [ ] 3.6 Implement COM offset caching: `m_com_offsets[rb_idx]` updated during Flush, exposed via `GetComOffsetLocal`
- [ ] 3.7 Implement `BindShapeToRigidBody` — establish binding in internal `rb_idx → shape_idx[]` map and `shape_idx → rb_idx` map
- [ ] 3.8 Implement `IsPhysicsActive` / `SetPhysicsActive` with default `false`
- [ ] 3.9 Create `engine/Framework/world/physics/PhysicsAdaptor.cpp` with all implementations

## 4. Scene integration

- [ ] 4.1 Add `std::unique_ptr<PhysicsAdaptor> m_physics_adaptor` to Scene
- [ ] 4.2 Implement `Scene::GetPhysicsAdaptor()` — lazy-create on first access, creating underlying `PhysicsScene` if needed
- [ ] 4.3 Implement `Scene::FlushPhysics(RenderSystem&)` — calls `m_physics_adaptor->Flush(render_system)`
- [ ] 4.4 Update `Scene::GetPhysicsScene()` to work alongside Adaptor (both may need the same PhysicsScene reference)

## 5. Component migration

- [ ] 5.1 Rewrite `RigidBodyComponent::Awake` — call `adaptor->AllocateSlot(handle)`, store index. Remove property value passing.
- [ ] 5.2 Rewrite `RigidBodyComponent::Init` — build `RigidBodyDescriptor` from component fields + GO transform, call `adaptor->SubmitRigidBody(idx, desc)`. Call `CollectShapesRecursivelyAndBind(adaptor)`.
- [ ] 5.3 Update `RigidBodyComponent::CollectShapesRecursively` — use Adaptor's `BindShapeToRigidBody` instead of direct PhysicsScene calls
- [ ] 5.4 Update `RigidBodyComponent::~RigidBodyComponent` — use Adaptor (or PhysicsScene via Adaptor) for unregistration
- [ ] 5.5 Rewrite `CollisionShapeComponent::Awake` — call `adaptor->AllocateShapeSlot(handle)`, store index. Remove world transform computation and property passing.
- [ ] 5.6 Rewrite `CollisionShapeComponent::Init` — extract `BuildDescriptor(owner)` private method (handles cylinder fallback once), call `adaptor->SubmitShape(idx, desc)`, call `TryAttachToAncestorRigidBody(adaptor)`.
- [ ] 5.7 Remove duplicated cylinder fallback code from `CollisionShapeComponent::Awake`
- [ ] 5.8 Rewrite `PhysicsConstraintComponent::Awake` — call `adaptor->AllocateFixedJoint()` / `AllocateHingeJoint()` per joint definition
- [ ] 5.9 Rewrite `PhysicsConstraintComponent::Init` — resolve obj2_handle via `adaptor->FindRigidBodyByObjectHandle`, compute GO-local relative transform, build `FixedJointSubmitData` / `HingeJointSubmitData`, call `adaptor->SubmitJoint(idx, data)`

## 6. RendererComponent update

- [ ] 6.1 Update `RendererComponent::PreRenderUpdate` — query `adaptor->FindRigidBodyByObjectHandle()` instead of `PhysicsScene::FindRigidBodyByObjectHandle()`
- [ ] 6.2 Update push-constant model matrix computation — use `adaptor->GetComOffsetLocal(rb_idx)` to compute offset matrix: translate offset, compose with renderer local transform
- [ ] 6.3 Use `adaptor->IsPhysicsActive()` to decide whether to enter physics-driven rendering path

## 7. PhysicsSystem and main loop update

- [ ] 7.1 Remove `PhysicsSystem::InitializePendingRigidBodies` method
- [ ] 7.2 Update `MainClass::RunOneFrame` / `MainClass::LoadProject` — replace `physics->InitializePendingRigidBodies(render_system)` with scene-level `FlushPhysics`
- [ ] 7.3 Update solver registration in `MainClass::LoadProject` — use `scene.GetPhysicsScene()` (still available) for solver binding
- [ ] 7.4 Update `PhysicsSystem::RegisterSolver` if it references removed PhysicsScene members

## 8. Editor and example updates

- [ ] 8.1 Update `example/editor_run_game_example/main.cpp` — replace `physics->InitializePendingRigidBodies(render_system)` with `scene.FlushPhysics(render_system)`, update Play/Stop to use `adaptor->SetPhysicsActive`
- [ ] 8.2 Update `example/physics_example/SceneBuilder` — change `Finalize(PhysicsScene&)` to `Finalize()`, use `scene.FlushPhysics`
- [ ] 8.3 Update example `SimulationToggleComponent` — verify it still works with `PhysicsScene::SetSimulationEnabled` (unchanged interface)
- [ ] 8.4 Update editor Play callback — set `adaptor->SetPhysicsActive(true)`; Stop callback — set `adaptor->SetPhysicsActive(false)`

## 9. Shader and GPU update

- [ ] 9.1 Update `SpatialHashBroadDetector` compute shaders (`compute_cell_aabbs.comp`, `generate_broad_pairs.comp`) — compute shape world position from COM pose + shape local pose instead of reading `shape_world_position` buffer
- [ ] 9.2 Remove `shape_world_position` and `shape_world_rotation` buffer bindings from solver descriptor set layouts
- [ ] 9.3 Remove `rigid_body_shape_offset`, `rigid_body_shape_count`, `flattened_shape_indices` buffer bindings (unused in current shaders)

## 10. Cleanup and verification

- [ ] 10.1 Run `clang-format -i` on all changed files
- [ ] 10.2 Build full project with `cmake --build build` and fix all compilation errors
- [ ] 10.3 Run existing physics tests: `test/physics_registration_test.cpp` (update if it references removed PhysicsScene members)
- [ ] 10.4 Manual smoke test: launch editor, create physics objects, press Play, verify objects fall and collide
- [ ] 10.5 Verify Physics/ module has zero Framework #includes: `grep -r "Framework/" engine/Physics/ --include="*.h" --include="*.cpp"`
