## Why

The engine currently has a complete GPU rigid-body physics pipeline and an OBJ/GLTF mesh import system, but lacks the ability to import robot models defined in the URDF format (Universal Robot Description Format) — the standard in robotics. A1 and H1 robot URDF assets already exist in the project. Adding URDF import enables robotics simulation use cases and validates the physics joint/constraint system end-to-end with a real-world multi-body robot.

## What Changes

- **New**: URDF file importer (`UrdfLoader`) that parses URDF XML and produces a `SceneAsset` prefab with full GameObject hierarchy, rigid bodies, collision shapes, joint constraints, and render meshes (using collision geometry for rendering until DAE/STL mesh import is implemented)
- **New**: `RigidBodyComponent` gains manual inertia tensor fields (`m_use_manual_inertia`, `m_manual_inertia_diag`, `m_manual_inertia_offdiag`) so URDF inertial data can be used instead of auto-computed values
- **New**: `PhysicsScene` gains a per-rigid-body flag and storage for manual inertia, with `RecalculateRigidBodyState` checking this flag before invoking automatic computation
- **Modified**: `PhysicsConstraintComponent` gains custom `save_to_archive`/`load_from_archive` overrides to serialize the `m_joints` variant vector, which the reflection system cannot handle for `std::variant`
- **New**: A `tinyxml2` dependency for XML parsing

## Capabilities

### New Capabilities

- `urdf-import`: Parse URDF XML files and produce GameObject prefabs with correct hierarchy, rigid bodies (mass + manual inertia), collision shapes (box/sphere/cylinder), joint constraints (revolute/hinge + fixed), and render meshes derived from collision geometry using builtin assets
- `manual-inertia-tensor`: Allow RigidBodyComponent to specify a manually-defined 3×3 inertia tensor that overrides the automatic volume-weighted computation in PhysicsScene, enabling URDF inertial data to be used directly
- `physics-constraint-serialization`: Custom archive support for PhysicsConstraintComponent so its `std::variant<FixedJointDef, HingeJointDef>` joint list survives scene save/load round-trips

### Modified Capabilities

- `shape-inertia-functions`: The inertia computation path in `PhysicsScene::RecalculateRigidBodyState` now conditionally skips automatic computation when a manual inertia flag is set on the rigid body

## Impact

- **New files**: `engine/Asset/Loader/UrdfLoader.h/.cpp`, `engine/Asset/Loader/UrdfTypes.h`, `third_party/tinyxml2/` (2 source files)
- **Modified files**: `engine/Asset/Loader/Importer.cpp` (add `.urdf` dispatch), `engine/Framework/component/physics/RigidBodyComponent.h/.cpp` (add manual inertia fields + Awake logic), `engine/Framework/component/physics/PhysicsConstraintComponent.h/.cpp` (add custom serialization), `engine/Physics/PhysicsScene.h/.cpp` (add manual inertia flag + early-return in RecalculateRigidBodyState), `engine/CMakeLists.txt`, `third_party/CMakeLists.txt`
- **Dependencies**: `tinyxml2` (new, header-only-style 2-file XML parser, permissively licensed)
- **No breaking changes**: All existing physics fields and APIs remain unchanged; manual inertia defaults to off
