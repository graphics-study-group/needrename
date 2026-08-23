# urdf-import

## Purpose

Import URDF robot descriptions into the engine: parse URDF XML into an intermediate representation, build the GameObject hierarchy with rigid bodies, collision shapes, joint constraints, and visuals, and persist as SceneAsset prefabs.

## ADDED Requirements

### Requirement: UrdfBuiltJoint carries the converted hinge axis

`UrdfBuiltJoint` SHALL contain an `axis` field holding the hinge axis of the corresponding URDF joint, converted to engine coordinates in the parent link's GO frame via the same conversion used for the `HingeJointDef` (`UrdfAxisToEngine(joint.axis)`).

The field SHALL be populated for joints that produced a physical `HingeJointDef` (revolute/continuous with rigid bodies on both ends). For fixed joints or joints without a physical constraint, the field SHALL be left at its default (identity-like, e.g. the `UrdfJoint.axis` default) and consumers SHALL ignore it.

This makes the joint axis available to consumers of `BuildRobotScene` (e.g. `PhysicsApp`) without re-implementing the URDF coordinate conversion.

#### Scenario: A1 hinge joint exposes the engine axis

- **WHEN** the A1 hierarchy is built and `FR_thigh_joint`'s `UrdfBuiltJoint` entry is inspected
- **THEN** its `axis` is approximately `(-1, 0, 0)` in engine coordinates (converted from URDF axis `0 1 0`), matching the `HingeJointDef.m_hinge_axis_obj1` of the corresponding constraint
