# go-descriptors

## MODIFIED Requirements

### Requirement: RigidBodyDescriptor struct

A `RigidBodyDescriptor` struct SHALL exist in `Framework/world/physics/PhysicsDescriptors.h` containing the following fields: `mass` (float), `static_friction` (float), `dynamic_friction` (float), `restitution` (float), `is_kinematic` (bool), `world_position` (vec3, GO world), `world_rotation` (quat, GO world), `linear_velocity` (vec3), `angular_velocity` (vec3), `external_force` (vec3), `external_torque` (vec3), `use_manual_inertia_com` (bool), `manual_inertia` (mat3), `manual_center_of_mass` (vec3, GO-local). The struct SHALL be copyable and movable.

The GO-space descriptors SHALL be Framework-internal transport structs carrying data from Framework components into `PhysicsAdaptor`; they SHALL NOT be part of the physics-interface (`PhysicsScene`) input contract.

#### Scenario: Descriptor built by RigidBodyComponent::Init
- **WHEN** `RigidBodyComponent::Init` runs
- **THEN** a `RigidBodyDescriptor` is constructed from component fields (m_mass, m_static_friction, etc.) and the GameObject's world transform

#### Scenario: Descriptor submitted to Adaptor
- **WHEN** `adaptor.SubmitRigidBody(idx, desc)` is called
- **THEN** the descriptor is stored in Adaptor's pending map for that index
