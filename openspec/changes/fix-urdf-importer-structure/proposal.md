## Why

The URDF importer (`UrdfLoader::BuildAndSaveSceneAsset`) has several structural bugs: the center-of-mass offset from `<inertial>/<origin>` is never set on `RigidBodyComponent`, the hinge joint axis is not rotated through `joint.origin.rpy`, collision/visual shapes are inconsistently placed (sometimes on the link GO via `m_center`, sometimes on child GOs), and `PhysicsConstraintComponent` is placed on the parent link GO requiring unnecessary axis rotation. These issues cause incorrect COM positions, wrong joint constraint rest states, and inconsistent GO hierarchy.

## What Changes

- **Fixed**: `RigidBodyComponent::m_manual_center_of_mass` now set from URDF `<inertial>/<origin>` in GO-local coordinates
- **Fixed**: Inertia tensor rotated from URDF inertial frame to link frame when `<inertial>/<origin>` has non-zero `rpy`: `I_link = Rᵀ * I * R`
- **Changed**: `PhysicsConstraintComponent` moved from **parent** link GO to **child** link GO. `m_hinge_anchor_obj1` becomes `(0, 0, 0)` (pivot at child GO origin = joint origin). `m_hinge_axis_obj1` uses `UrdfAxisToEngine(joint.axis)` directly — no `rpy` rotation needed. `m_obj2_handle` references the parent link GO
- **Changed**: Every `<collision>` element always creates a dedicated child GO with `CollisionShapeComponent` (`m_center = 0, m_rotation = 0`). Collision offset is encoded in the child GO's `Transform`
- **New**: Every `<collision>` element also creates a dedicated visual child GO with `StaticMeshComponent` (Phase 1: visual geometry sourced from collision data). Mesh scale encoded in the visual child GO's `Transform`
- **Changed**: `PhysicsConstraintComponent` only created when **both** parent and child links have `<inertial>` (RigidBodyComponent). Links without inertial are skipped — their collision shapes are collected by ancestor RigidBody
- **Removed**: `parent_constraints` aggregation map — no longer needed since constraint lives on child side

## Capabilities

### New Capabilities

*(none)*

### Modified Capabilities

- `urdf-import`: GO hierarchy structure changed — constraint on child, always child GOs for collision/visual, COM offset populated, inertia tensor rotated, link naming with indices
- `manual-inertia-tensor`: `m_manual_center_of_mass` now populated from URDF `<inertial>/<origin>` data by the importer

## Impact

- **Modified**: `engine/Asset/Loader/UrdfLoader.cpp` — `BuildAndSaveSceneAsset` restructured (steps 2-7 rewritten), `CollectCollisionObjectHandles` unchanged (subtree traversal still works)
- **Modified**: `engine/Asset/Loader/UrdfLoader.h` — no API changes needed
- **No changes**: `RigidBodyComponent`, `CollisionShapeComponent`, `PhysicsConstraintComponent`, `PhysicsScene` — these already support the new usage pattern
- **ADR**: `docs/adr/0006-urdf-constraint-on-child.md` (already written)
- **Glossary**: `CONTEXT.md` § URDF Import Hierarchy (already updated)
