# physics-renderer-ssbo-toggle Specification

## Purpose
TBD - created by archiving change integrate-physics-into-main-loop. Update Purpose after archive.
## Requirements
### Requirement: PreRenderUpdate checks model matrix active flag

`RendererComponent::PreRenderUpdate` SHALL only set `model_mat_index` to the rigid body index when `PhysicsScene::IsModelMatrixActive(rigid_idx)` returns `true`. Otherwise, it SHALL set `model_mat_index` to `-1`.

#### Scenario: Physics active — SSBO path used

- **WHEN** `PreRenderUpdate` finds a rigid body in the ancestor chain
- **AND** `IsModelMatrixActive(rigid_idx)` returns `true`
- **THEN** `model_mat_index` is set to `rigid_idx`
- **AND** the model matrix is converted to local space relative to the rigid body's world transform

#### Scenario: Physics inactive — push-constant path used

- **WHEN** `PreRenderUpdate` finds a rigid body in the ancestor chain
- **AND** `IsModelMatrixActive(rigid_idx)` returns `false`
- **THEN** `model_mat_index` is set to `-1`
- **AND** the model matrix is the full world transform from the owning GameObject

### Requirement: PhysicsScene stores and queries model matrix active state

`PhysicsScene` SHALL maintain a `std::vector<bool> m_rigid_body_model_matrix_active` parallel to rigid body slots. `SetModelMatrixActive(idx, bool)` SHALL set the entry. `IsModelMatrixActive(idx)` SHALL return the entry.

#### Scenario: Rigid body activated

- **WHEN** `SetModelMatrixActive(3, true)` is called
- **THEN** subsequent `IsModelMatrixActive(3)` returns `true`

#### Scenario: Simulation disabled clears all

- **WHEN** `SetSimulationEnabled(false)` is called
- **THEN** all entries in `m_rigid_body_model_matrix_active` are set to `false`
- **AND** `IsModelMatrixActive` returns `false` for every registered rigid body

### Requirement: RigidBodyComponent Init activates model matrix

`RigidBodyComponent::Init` SHALL call `SetModelMatrixActive(m_rigid_body_index, true)` after uploading property data.

#### Scenario: Play transitions renderer to SSBO path

- **WHEN** `RigidBodyComponent::Init` completes during a Play transition
- **THEN** the next call to `PreRenderUpdate` for descendant renderers finds `IsModelMatrixActive == true`
- **AND** `model_mat_index` is set to the rigid body index, enabling SSBO-driven rendering

### Requirement: Renderer falls back to TransformComponent when SSBO unavailable

When a renderer component's ancestor chain contains a rigid body but either the model matrix is not active or the physics SSBO buffer is not yet created, the renderer SHALL use the push-constant model matrix from `TransformComponent` (full world transform).

#### Scenario: Before first Play

- **WHEN** a scene is loaded with physics components but Play has not been pressed
- **AND** `PreRenderUpdate` runs
- **THEN** renderers under rigid bodies use `model_mat_index = -1`
- **AND** object positions match `TransformComponent` data

#### Scenario: After Stop

- **WHEN** the editor Stop button is pressed and `SetSimulationEnabled(false)` is called
- **AND** `PreRenderUpdate` runs on the next frame
- **THEN** renderers under rigid bodies use `model_mat_index = -1`
- **AND** object positions match `TransformComponent` data (last simulation state persisted in transforms)

