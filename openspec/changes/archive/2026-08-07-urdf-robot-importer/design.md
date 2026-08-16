# URDF Robot Importer — Design Document

## Context

The engine has a complete GPU rigid-body physics pipeline (XPBD solver, collision detection, fixed/hinge joint constraints) and an OBJ/GLTF mesh import system. A1 and H1 robot URDF assets already exist in the project. We need to add URDF import capability to convert robot models into the engine's GameObject hierarchy and serialize them as SceneAsset prefabs.

Key architectural constraints of the engine:
- **Reflection-based serialization**: Compile-time code generation via Python + libclang. `REFL_SER_ENABLE` marks serializable fields. `std::variant` is not supported by the reflection system.
- **Physics component lifecycle**: `Awake()` is called during `FlushCmdQueue()` and requires `PhysicsScene` to be available for registration. A temp scene without physics will cause Awake to skip gracefully.
- **Prefab system**: `SceneAsset::SaveFromScene()` serializes an entire Scene → `AddToScene()` deserializes into a target scene with handle remapping.

## Goals / Non-Goals

**Goals:**
- Parse URDF XML files and build GameObject hierarchies (link → GO, joint → parent-child + constraint)
- Correctly map URDF coordinate system (X=forward, Y=left, Z=up) to engine coordinate system (X=right, Y=forward, Z=up)
- Support collision shapes (box/sphere/cylinder) for physics, and render meshes from the same collision geometry (using builtin mesh assets)
- Support manual inertia tensor override (URDF inertial data used directly)
- Support serialization of fixed and revolute joint constraints for scene save/load round-trips
- Produce a SceneAsset prefab loadable into a simulation scene

**Non-Goals:**
- DAE/STL mesh file import (Phase 2)
- Joint angle limits and damping (engine HingeJoint does not support them yet)
- Prismatic and floating joint support
- Gazebo extension tag parsing
- Transmission element handling
- Immediate physics simulation after import (serialization only)

## Decisions

### D1: tinyxml2 as XML parsing library

**Choice**: tinyxml2 (2 source files, compiled directly into the engine)

**Alternatives considered**:
- pugixml: more features but larger; uncommon in the ROS URDF ecosystem
- rapidxml: header-only but older API

**Rationale**: tinyxml2 is the de facto standard in the ROS URDF ecosystem, trivial to integrate, and permissively licensed.

### D2: Use collision geometry for rendering instead of visual geometry

**Choice**: Render using `<collision>` geometry (box/sphere/cylinder) via the engine's builtin mesh assets (`~/mesh/cube.asset`, `~/mesh/sphere.asset`, `~/mesh/cylinder.asset`).

**Alternatives considered**:
- Render `<visual>` geometry: the primary visual geometry for these robots is stored in DAE/STL mesh files, which are not yet supported by the engine. Reading visual would produce mostly empty renders.
- Generate procedural geometry: unnecessary complexity when builtin meshes already exist.

**Rationale**: Until DAE/STL mesh import is implemented (Phase 2), `<collision>` geometry provides the most reliable rendering source — all collision elements use box/sphere/cylinder primitives that map directly to builtin meshes. Collision shapes and render shapes share the same data source, ensuring they are visually aligned.

### D3: Manual inertia tensor storage scheme

**Choice**: Add 3 `REFL_SER_ENABLE` fields to `RigidBodyComponent` (`m_use_manual_inertia` bool + 2 `vec3`), and add 2 CPU-side parallel arrays to `PhysicsScene` (`m_rigid_body_use_manual_inertia` vector of bool + `m_rigid_body_manual_inertia` vector of mat3).

**Alternatives considered**:
- Store `glm::mat3` directly: the reflection system does not support `glm::mat3` (only `glm::vec3`, `glm::vec4`, `glm::quat`, `glm::mat4`)
- Use 9 separate floats: too verbose

**Rationale**: Splitting the symmetric 3×3 inertia into 2 `vec3` (diagonal and off-diagonal) uses glm types already supported by the reflection system, giving automatic serialization. The `PhysicsScene` parallel arrays do not need GPU sync — `RecalculateRigidBodyState` runs on the CPU, checks the flag, and either copies the manual value or runs the automatic computation, both writing to `m_rigid_body_inertia` (which already has an existing GPU upload path).

### D4: PhysicsConstraintComponent serialization — manual override

**Choice**: Add `save_to_archive` / `load_from_archive` overrides on `PhysicsConstraintComponent` to manually serialize the `std::variant<FixedJointDef, HingeJointDef>` vector. ObjectHandle fields are written as raw uint32_t via `GetID()`, and remapped automatically by `HandleResolver` on load.

**Alternatives considered**:
- Extend the reflection system to support `std::variant`: massive effort, touches all generated code
- Refactor JointDef into an inheritance hierarchy: not consistent with existing engine code style

**Rationale**: Manual serialization is the least invasive approach. The `Component` base class already supports `save_to_archive`/`load_from_archive` virtual functions, which take priority over generated code. `HandleResolver` is already provided during `SceneAsset::AddToScene` with ID→Handle mapping, usable directly.

### D5: Two-pass FlushCmdQueue strategy

**Choice**: Call `FlushCmdQueue()` once after adding all RigidBody + CollisionShape components, then add PhysicsConstraint + StaticMesh components, then flush once more.

**Rationale**: `PhysicsConstraintComponent::Awake()` needs to look up RigidBody components by ObjectHandle. The first flush ensures RigidBody components are instantiated (even though physics registration is skipped in the temp scene). In practice, a single flush works in the temp scene (no PhysicsScene), but two flushes are more robust and make the instantiation order explicit.

### D6: Material selection — deterministic hash

**Choice**: Use `std::hash` on the link name to deterministically select from the 9 builtin PBR solid color materials.

**Rationale**: Avoids non-deterministic prefab content across imports. Different links get different colors to aid visual identification.

## Coordinate System Conversion

```
URDF:    X=forward, Y=left, Z=up
Engine:  X=right, Y=forward, Z=up

Position/Direction transform:
  eng.x = -urdf.y
  eng.y =  urdf.x
  eng.z =  urdf.z

Rotation transform (RPY → quat):
  q_urdf = fromRPY(r, p, y)       // URDF fixed-axis X→Y→Z
  q_eng  = q_convert * q_urdf     // q_convert = angleAxis(+90°, Z)
```

## Data Flow

```
URDF XML → UrdfParser → UrdfRobot (IR)
                            │
                    ┌───────┼───────┐
                    ▼       ▼       ▼
              links[]  joints[]  materials[]
                    │       │
                    ▼       ▼
              BuildGOs()  BuildConstraints()
                    │       │
                    ▼       ▼
              temp_scene  (GameObject tree + Components)
                    │
                    ▼
              FlushCmdQueue()
                    │
                    ▼
              SceneAsset::SaveFromScene()
                    │
                    ▼
              SaveAsset() → GO_<name>.asset
```

### D7: Every URDF link gets an independent GameObject

**Choice**: Every URDF link, including those connected by fixed joints and collision-only links (e.g. `thigh_shoulder`), SHALL get its own independent GameObject. The hierarchy SHALL exactly mirror the URDF link/joint tree.

**Rationale**: Fidelity to the source URDF. Fixed joints still create a `PhysicsConstraintComponent`, and `CollectShapesRecursively` will naturally aggregate collision shapes up to the nearest ancestor with a `RigidBodyComponent`. Collision-only links that have no `<inertial>` simply don't get a `RigidBodyComponent`, and their collision shapes are collected by the nearest rigid-body ancestor through the existing traversal rules.

### D8: Parent-child collision ignoring

**Choice**: For each URDF joint, populate `m_ignore_collision_objects` on every `CollisionShapeComponent` of the child link with the ObjectHandle of every GameObject in the parent link's subtree that carries a `CollisionShapeComponent`. Do the same on the parent side (add child's collision GOs to parent's ignore list) for bidirectionality.

**Rationale**: URDF joints physically connect two links — they should not collide with each other at the joint interface. The engine's `CollisionShapeComponent::m_ignore_collision_objects` provides exactly this filtering mechanism. Without it, the XPBD solver would fight self-collisions at every joint, producing unstable or exploded simulations.

### CollisionShapeComponent m_center/m_rotation Semantics

**Verified**: `m_center` and `m_rotation` are **inputs** to `Awake()`, not outputs. The formula in `CollisionShapeComponent::Awake()` (line 40-42) is:

```
world_center   = GO_world_pos + GO_world_rot * m_center
world_rotation = normalize(GO_world_rot * m_rotation)
```

`m_center` is a local offset from the GameObject origin in the GO's local frame. `m_rotation` is a local rotation relative to the GO's rotation. Both are serialized via `REFL_SER_ENABLE` and **never overwritten by Awake**. This maps directly to URDF collision `<origin xyz rpy>` — the offset of the collision shape relative to the link frame.

No `"use_manual_center"` flag is needed. The existing design is correct as-is.

## Risks / Trade-offs

- **[Risk] Rendering uses collision geometry instead of visual geometry** → Robot visuals use simple geometric primitives matching the collision shapes, without DAE/STL mesh detail. This is intentional for Phase 1. Mitigation: Phase 2 adds DAE/STL mesh import, at which point rendering will switch to `<visual>` geometry.
- **[Risk] No joint angle limits** → The HingeJoint shader does not support angle limits; A1 knees may hyperextend. Mitigation: Phase 2 extends the shader and constraint component.
- **[Risk] FixedJoint initial relative transform may differ between temp scene and main scene** → FixedJoint's initial_rel_pos_local is computed during Awake from world transforms. In the temp scene, Awake skips (no PhysicsScene). Actual computation happens after main scene load, by which time the GO hierarchy is correctly restored, so initial values should be correct.
- **[Risk] Collision shapes collected by wrong RigidBody** → A collision-only link (no RigidBody) under a fixed joint will have its shapes collected by the nearest rigid-body ancestor via `CollectShapesRecursively`. This is the desired behavior — the collision shape belongs to the rigid body it's fixed to. The URDF importer must ensure the GO hierarchy is correct so traversal reaches the right ancestor.
