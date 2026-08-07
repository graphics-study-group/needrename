# physics-constraint-serialization Specification

## Purpose
TBD - created by archiving change urdf-robot-importer. Update Purpose after archive.
## Requirements
### Requirement: PhysicsConstraintComponent serializes m_joints

`PhysicsConstraintComponent` SHALL override `save_to_archive` and `load_from_archive` to manually handle the `m_joints` field, since the reflection system cannot serialize `std::variant`.

The `save_to_archive` override SHALL:
1. Call the base `Component::save_to_archive(archive)` to handle standard fields
2. Write `m_joints` as a JSON array, with each element tagged by `"type"` (`"fixed"` or `"hinge"`) and containing all fields of the corresponding joint definition

The `load_from_archive` override SHALL:
1. Call the base `Component::load_from_archive(archive)` to restore standard fields
2. Read the JSON `"m_joints"` array and reconstruct `FixedJointDef` or `HingeJointDef` objects based on the `"type"` tag

#### Scenario: Fixed joint round-trips through save/load
- **WHEN** a `PhysicsConstraintComponent` with a `FixedJointDef` (obj2_handle=5, compliance=0.0) is saved to an archive and then loaded
- **THEN** the loaded component has one joint in `m_joints`
- **AND** the joint is a `FixedJointDef`
- **AND** `m_obj2_handle.GetID()` is `5`
- **AND** `m_compliance` is `0.0f`

#### Scenario: Hinge joint round-trips through save/load
- **WHEN** a `PhysicsConstraintComponent` with a `HingeJointDef` (obj2_handle=3, compliance=0.0, axis=(1,0,0), attach=(0.1,0.2,0)) is saved to an archive and then loaded
- **THEN** the loaded component has one joint in `m_joints`
- **AND** the joint is a `HingeJointDef`
- **AND** all axis and attach point fields are restored to their original values

#### Scenario: Multiple joints round-trip
- **WHEN** a `PhysicsConstraintComponent` with one `FixedJointDef` and one `HingeJointDef` is saved and loaded
- **THEN** the loaded component has exactly two joints in `m_joints`
- **AND** they are of the correct types in the original order

#### Scenario: Empty joints round-trips
- **WHEN** a `PhysicsConstraintComponent` with an empty `m_joints` is saved and loaded
- **THEN** the loaded component has an empty `m_joints`

### Requirement: Serialized joints survive full scene save/load

When a `SceneAsset` containing `PhysicsConstraintComponent` instances is saved via `SaveFromScene` and loaded via `AddToScene`, all joint definitions SHALL be restored, including `ObjectHandle` references which SHALL be correctly remapped through the `HandleResolver`.

#### Scenario: Constraint handles are remapped on scene load
- **WHEN** a scene containing a `PhysicsConstraintComponent` with a `FixedJointDef` referencing obj2 handle 7 is saved and loaded into a new scene
- **THEN** the loaded joint references the correct new handle for the corresponding GameObject (not necessarily handle 7)
- **AND** `Awake()` succeeds in finding obj2's rigid body via the remapped handle

