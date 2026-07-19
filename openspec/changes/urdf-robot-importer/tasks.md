## 1. Dependencies & Infrastructure

- [x] 1.1 Integrate tinyxml2: place `tinyxml2.h` and `tinyxml2.cpp` into `third_party/tinyxml2/`, update `third_party/CMakeLists.txt`
- [x] 1.2 Add `.urdf` extension dispatch in `engine/Asset/Loader/Importer.cpp` `ImportExternalResource`, calling `UrdfLoader::LoadUrdfResource`
- [x] 1.3 Update `engine/CMakeLists.txt` to include new source files (GLOB_RECURSE auto-discovers; added tinyxml2 link)

## 2. Manual Inertia Tensor Support

- [x] 2.1 Add `REFL_SER_ENABLE bool m_use_manual_inertia{false}`, `REFL_SER_ENABLE glm::vec3 m_manual_inertia_diag{0}`, `REFL_SER_ENABLE glm::vec3 m_manual_inertia_offdiag{0}` fields to `RigidBodyComponent.h`
- [x] 2.2 Add `m_rigid_body_use_manual_inertia` (`std::vector<bool>`) and `m_rigid_body_manual_inertia` (`std::vector<glm::mat3>`) parallel arrays to `PhysicsScene.h`, plus `SetRigidBodyManualInertia(uint32_t, const glm::mat3&)` method declaration
- [x] 2.3 Implement `SetRigidBodyManualInertia` in `PhysicsScene.cpp`; extend both arrays to match current rigid body count in `RegisterRigidBody`
- [x] 2.4 Add manual inertia check at the start of `PhysicsScene::RecalculateRigidBodyState`: if flag is true, copy `m_rigid_body_manual_inertia[rb_index]` directly into `m_rigid_body_inertia[rb_index]`, compute inverse into `m_rigid_body_inverse_inertia[rb_index]`, then `return` skipping auto-computation
- [x] 2.5 In `RigidBodyComponent::Awake`, if `m_use_manual_inertia` is true, assemble a `glm::mat3` from `m_manual_inertia_diag`/`m_manual_inertia_offdiag` and call `SetRigidBodyManualInertia`

## 3. Physics Constraint Serialization

- [x] 3.1 Declare `void save_to_archive(Serialization::Archive&) const override` and `void load_from_archive(Serialization::Archive&) override` in `PhysicsConstraintComponent.h`
- [x] 3.2 Implement serialization in `PhysicsConstraintComponent.cpp`: write `m_joints` as a JSON array, each element tagged with `"type"` (`"fixed"`/`"hinge"`) and containing all fields; write ObjectHandle IDs as raw uint32_t via `GetID()`
- [x] 3.3 Implement deserialization in `PhysicsConstraintComponent.cpp`: read `m_joints` JSON array, reconstruct `FixedJointDef`/`HingeJointDef` by `"type"` tag; construct ObjectHandle from stored uint32_t (remapped by HandleResolver at load time)

## 4. URDF Parser

- [x] 4.1 Create `engine/Asset/Loader/UrdfTypes.h`: define `UrdfLink`, `UrdfJoint`, `UrdfGeometry`, `UrdfInertial`, `UrdfMaterial`, `UrdfRobot` intermediate representation structs
- [x] 4.2 Implement `UrdfParser`: use tinyxml2 to traverse `<robot>` children `<link>`, `<joint>`, `<material>`, populating the `UrdfRobot` struct
- [x] 4.3 Implement coordinate system conversion utilities: `UrdfToEnginePos`, `UrdfAxisToEngine`, `UrdfRpyToEngineQuat`
- [x] 4.4 Implement `package://` path resolution: map `package://<pkg>/<path>` to `{assets_dir}/<pkg>/<path>`

## 5. GameObject Hierarchy Construction

- [x] 5.1 Implement the main entry function `LoadUrdfResource(path, path_in_project)` in `UrdfLoader.cpp`: orchestrate parse → build → save flow
- [x] 5.2 Implement GO hierarchy construction: create one GameObject per link, set parent-child relationships and Transforms per joint (using coordinate conversion)
- [x] 5.3 Implement RigidBodyComponent attachment: add RigidBodyComponent to links with `<inertial>`, populating mass and manual inertia
- [x] 5.4 Implement CollisionShapeComponent attachment: add CollisionShapeComponent per `<collision>` element, mapping shape type/feature/center/rotation; for non-zero collision origins, create child GOs to hold the shapes
- [x] 5.5 Implement PhysicsConstraintComponent attachment: ONE component per parent link, collecting all joints into `m_joints` array; fixed/revolute joints compute attach points and axis vectors
- [x] 5.6 Implement parent-child collision filtering: for each joint, populate `m_ignore_collision_objects` bidirectionally — add parent's collision GOs to every child-side `CollisionShapeComponent`, and child's collision GOs to every parent-side `CollisionShapeComponent`
- [x] 5.7 Implement StaticMeshComponent attachment: create a mesh child GO per collision with proper scale (builtin cube=2×2×2→half_extents, sphere=r=1→vec3(r), cylinder=r=1,h=2,Z-up→(r,r,half_h)); add StaticMeshComponent with builtin mesh refs and hash-selected PBR materials
- [x] 5.8 Call `FlushCmdQueue()`, create `SceneAsset` via `SaveFromScene()`, persist via `SaveAsset()` as `GO_<robot_name>.asset`

## 6. Verification

- [x] 6.1 Build verification: compile the project and fix any errors
- [ ] 6.2 Write example or test: load `a1.urdf`, import into temp project, verify `GO_a1.asset` file exists
- [ ] 6.3 Verify serialization correctness: load into main scene via `SceneAsset::AddToScene()`, confirm all GameObjects, RigidBody, CollisionShape, Constraint, and StaticMesh components are correctly restored
