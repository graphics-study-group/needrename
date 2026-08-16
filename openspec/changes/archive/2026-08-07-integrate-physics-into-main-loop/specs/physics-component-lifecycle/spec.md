## ADDED Requirements

### Requirement: RigidBodyComponent Awake registers topology only

`RigidBodyComponent::Awake` SHALL register the rigid body with `PhysicsScene` via `RegisterRigidBody`, collect descendant `CollisionShapeComponent` instances via `CollectShapesRecursively`, and bind valid shapes via `SetCollisionShapeRigidBody`. It SHALL NOT call `EnqueueRigidBodyInitialization` or upload property data.

#### Scenario: First creation of RigidBodyComponent

- **WHEN** a `RigidBodyComponent` is first added to a scene and `FlushCmdQueue` is called
- **THEN** `RegisterRigidBody` is called with the owning GameObject's world transform
- **AND** `m_rigid_body_index` is cached from the registration
- **AND** descendant `CollisionShapeComponent` instances are collected and bound

#### Scenario: RigidBodyComponent on scene reload

- **WHEN** a scene is reloaded and the same `RigidBodyComponent` instance's `Awake` is called again
- **THEN** `FindRigidBodyByObjectHandle` finds the existing registration
- **AND** no new rigid body slot is allocated

### Requirement: RigidBodyComponent Init uploads property data

`RigidBodyComponent::Init` SHALL upload the current GameObject world transform via `SetRigidBodyTransform`, all serialized properties via `SetRigidBodyProperties` (mass, friction, restitution, kinematic flag, velocities, forces), manual inertia if configured, and SHALL call `EnqueueRigidBodyInitialization` and `SetModelMatrixActive(true)`.

#### Scenario: Editor Play starts

- **WHEN** the editor Start button is pressed and `Init` events are dispatched
- **THEN** `SetRigidBodyTransform` is called with the current world position and rotation from the owning GameObject
- **AND** `SetRigidBodyProperties` is called with the component's current serialized property values
- **AND** `SetModelMatrixActive(m_rigid_body_index, true)` is called
- **AND** `EnqueueRigidBodyInitialization(m_rigid_body_index)` is called

#### Scenario: No parent GameObject available

- **WHEN** `Init` is called but the parent GameObject is null
- **THEN** the method returns early with a warning log
- **AND** no `PhysicsScene` methods are called

### Requirement: CollisionShapeComponent Awake registers topology only

`CollisionShapeComponent::Awake` SHALL register the collision shape with `PhysicsScene` via `RegisterCollisionShape` and call `TryAttachToAncestorRigidBody`. It SHALL NOT update geometry data.

#### Scenario: First creation of CollisionShapeComponent

- **WHEN** a `CollisionShapeComponent` is first added to a scene and `FlushCmdQueue` is called
- **THEN** `RegisterCollisionShape` is called with the component's world transform, type, feature, and ignore list
- **AND** `m_shape_index` is cached from the registration
- **AND** `TryAttachToAncestorRigidBody` is called (may fail silently if ancestor RB not yet registered)

### Requirement: CollisionShapeComponent Init uploads geometry data

`CollisionShapeComponent::Init` SHALL update the collision shape geometry in `PhysicsScene` via `UpdateCollisionShapeGeometry` and re-attempt attachment to ancestor rigid body via `TryAttachToAncestorRigidBody`.

#### Scenario: Editor Play starts

- **WHEN** the editor Start button is pressed and `Init` events are dispatched
- **THEN** `UpdateCollisionShapeGeometry` is called with current world position, rotation, type, feature, and ignore list
- **AND** `TryAttachToAncestorRigidBody` is called (guaranteed to succeed because all RB Awakes have completed)

### Requirement: PhysicsConstraintComponent Awake allocates joint slots

`PhysicsConstraintComponent::Awake` SHALL allocate placeholder joint slots in `PhysicsScene` via `AllocateFixedJoint` and `AllocateHingeJoint` for each joint definition. It SHALL NOT resolve handles or register joint data.

#### Scenario: First creation with joints

- **WHEN** a `PhysicsConstraintComponent` with two joints (one fixed, one hinge) is added
- **THEN** `AllocateFixedJoint` is called once, returning a valid index
- **AND** `AllocateHingeJoint` is called once, returning a valid index
- **AND** the indices are cached on the component

### Requirement: PhysicsConstraintComponent Init resolves and uploads joint data

`PhysicsConstraintComponent::Init` SHALL resolve stored `ObjectHandle` references to rigid body indices, compute initial relative transforms for fixed joints, and call `UpdateFixedJoint`/`UpdateHingeJoint` with resolved data.

#### Scenario: Editor Play starts after all RBs are registered

- **WHEN** the editor Start button is pressed and all RigidBody Awake callbacks have completed
- **THEN** `FindRigidBodyByObjectHandle` finds both obj1 and obj2 for each joint
- **AND** `UpdateFixedJoint` or `UpdateHingeJoint` is called with resolved indices and transform data

### Requirement: Simulation disabled clears SSBO activation

When `PhysicsScene::SetSimulationEnabled(false)` is called, all `m_rigid_body_model_matrix_active` entries SHALL be set to `false`.

#### Scenario: Editor Stop button pressed

- **WHEN** the user presses the Stop button in the editor
- **THEN** `SetSimulationEnabled(false)` is called
- **AND** `IsModelMatrixActive` returns `false` for all registered rigid bodies
