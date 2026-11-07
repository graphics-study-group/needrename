# physics-main-loop-integration Delta

## MODIFIED Requirements

### Requirement: Default solver auto-registration

Upon loading a project that contains physics components (`RigidBodyComponent` instances in the main scene), the engine SHALL automatically register a default `XpbdGpuSolver`. If no physics components exist in the scene, no solver SHALL be registered.

`MainClass::LoadProject` SHALL accept an optional `XpbdConfig` parameter (pointer, defaulting to none). When provided, the registered solver SHALL use the provided configuration as its `XpbdConfig`; when omitted, the solver SHALL use the engine's hardcoded configuration defaults. The default project path through `PhysicsApp` SHALL pass the config derived from `CreateInfo`'s physics timing knobs. This parameter is a temporary seam until project-level configuration exists; it SHALL NOT grow into per-field overrides.

#### Scenario: Project with physics objects loads successfully

- **WHEN** `MainClass::LoadProject` is called with a project containing at least one `RigidBodyComponent`
- **THEN** a `XpbdGpuSolver` is registered with `PhysicsSystem` for the main scene's physics scene
- **AND** the solver uses the engine's default `XpbdConfig` values when no config parameter was provided

#### Scenario: Provided config reaches the solver

- **WHEN** `MainClass::LoadProject` is called with a `XpbdConfig` whose `time_step` is 0.005
- **THEN** the registered solver steps with a 0.005 s time step

#### Scenario: Project without physics objects loads silently

- **WHEN** `MainClass::LoadProject` is called with a project containing zero `RigidBodyComponent` instances
- **THEN** no solver is registered
- **AND** no error or warning is emitted
