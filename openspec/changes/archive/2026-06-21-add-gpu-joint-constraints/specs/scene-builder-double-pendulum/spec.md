# scene-builder-double-pendulum

## Purpose

Define the SceneBuilder convenience method that constructs a double pendulum demonstration for visual verification of joint constraints.

## Requirements

### Requirement: SceneBuilder AddDoublePendulum method

SceneBuilder SHALL provide a method `AddDoublePendulum(glm::vec3 anchor_position, float spacing)` that creates a double pendulum assembly. The structure SHALL be:

1. A **kinematic sphere** at `anchor_position` acting as the fixed anchor point
2. A **HingeJoint** connecting the sphere to the first dynamic box
3. A **dynamic box** (first pendulum link)
4. A **HingeJoint** connecting the first box to the second dynamic box
5. A **dynamic box** (second pendulum link)
6. A **FixedJoint** connecting the second box to a dynamic cylinder
7. A **dynamic cylinder** rigidly attached to the second box

The spacing parameter SHALL control the gap between connected bodies to prevent collision.

#### Scenario: Method creates complete double pendulum

- **WHEN** `AddDoublePendulum(glm::vec3(0,0,3), 0.1f)` is called
- **THEN** 4 GameObjects are created (sphere, box1, box2, cylinder)
- **AND** 2 HingeJoints and 1 FixedJoint are registered on their respective owner objects
- **AND** the sphere has kinematic=true and mass=0

### Requirement: Anchor sphere properties

The anchor sphere SHALL be kinematic (mass=0, kinematic=true) to remain fixed in space. It SHALL use a visually distinct material. Its radius SHALL be 0.3.

#### Scenario: Anchor sphere does not fall

- **WHEN** simulation starts
- **THEN** the anchor sphere remains at its initial position
- **AND** the pendulum links hang from it

### Requirement: Pendulum boxes properties

Each pendulum box SHALL be a dynamic rigid body (mass=1.0) with half-extents that create a visually elongated rectangular shape. The hinge attachment points SHALL be placed at the ends of each box along its long axis, with the `spacing` parameter adding clearance.

#### Scenario: Boxes swing under gravity

- **WHEN** simulation runs with gravity along -Z
- **THEN** both boxes swing as a double pendulum
- **AND** boxes do not separate at the hinge points

### Requirement: Bottom cylinder rigidly attached

A cylinder SHALL be attached to the bottom of the second box via a FixedJoint. The cylinder SHALL be oriented horizontally (90° rotation around the Y axis) so that its rotation is visually apparent when the pendulum swings. The cylinder SHALL be dynamic (mass=0.5).

#### Scenario: Cylinder follows box rotation

- **WHEN** the second box swings and rotates
- **THEN** the cylinder follows with the same rigid-body transform
- **AND** the cylinder does not separate from the box

### Requirement: Joint validation in the demo

All joints in the demonstration SHALL pass validation (both obj1 and obj2 have RigidBodyComponent). No error messages SHALL be logged for the demo joints.

#### Scenario: All demo joints registered successfully

- **WHEN** the physics example runs
- **THEN** `PhysicsScene::DebugPrint()` shows exactly 2 hinge joints and 1 fixed joint registered
- **AND** no joint validation errors appear in the log output
