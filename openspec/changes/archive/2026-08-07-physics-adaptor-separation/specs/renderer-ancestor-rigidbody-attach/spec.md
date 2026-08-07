## MODIFIED Requirements

### Requirement: Renderer finds owning RigidBody via ancestor walk
`RendererComponent::PreRenderUpdate()` SHALL walk up the GameObject ancestor chain to find a registered RigidBody. Starting from the renderer's owning GameObject, it SHALL check each ancestor via `PhysicsAdaptor::FindRigidBodyByObjectHandle()`. The first ancestor with a valid rigid body index SHALL be used. The walk SHALL stop at the scene root (invalid parent handle). If no rigid body is found in the chain, `model_mat_index` SHALL remain `-1`.

#### Scenario: Mesh on child GO finds RigidBody on parent
- **WHEN** a StaticMeshComponent is on "Mesh" child GO, and its parent GO has a RigidBodyComponent registered with the Adaptor
- **THEN** `PreRenderUpdate()` finds the rigid body index from the parent GO via `adaptor->FindRigidBodyByObjectHandle()` and sets `model_mat_index` to that index

#### Scenario: Direct parent (no walk needed)
- **WHEN** a StaticMeshComponent is on the same GO as the RigidBodyComponent
- **THEN** `PreRenderUpdate()` finds the rigid body index on the immediate parent via Adaptor

#### Scenario: No RigidBody in ancestor chain
- **WHEN** a StaticMeshComponent's ancestor chain contains no GO with a registered RigidBody
- **THEN** `model_mat_index` remains `-1` (unchanged behavior)

#### Scenario: Nearest RigidBody wins
- **WHEN** the ancestor chain contains two GOs with RigidBodyComponents
- **THEN** the nearest one is used

### Requirement: Push-constant model carries local transform for physics-driven renderers
When a rigid body ancestor is found, the push-constant `model` matrix SHALL be set to the renderer's local-to-COM offset matrix. The offset matrix SHALL be computed using the Adaptor's `GetComOffsetLocal(rb_idx)` to account for the center-of-mass displacement. The offset matrix formula is: `translate(renderer_local_pos - com_offset_local) * mat4(renderer_local_rot)` where `renderer_local_pos` and `renderer_local_rot` are the renderer's position and rotation in GO-local space (from TransformComponent).

When no rigid body is found, the push-constant `model` matrix SHALL remain the renderer's GO world-space transform (unchanged behavior).

#### Scenario: Offset matrix computation
- **WHEN** the renderer is at GO-local position (0, 0, 0) with identity rotation
- **AND** the COM offset is `GetComOffsetLocal(rb_idx) = (0.2, 0, 0)`
- **THEN** the offset matrix translates by (-0.2, 0, 0)

#### Scenario: Mesh at same position as rigid body GO
- **WHEN** the "Mesh" child GO has an identity local transform relative to the rigid body's GO
- **THEN** the offset matrix translates by the negative COM offset (the renderer needs to account for the COM displacement)

### Requirement: Shader composes rigid body matrix with local transform
The vertex shader's `get_model_matrix()` function SHALL compute the final model matrix as `model_matrices.m[pc.model_mat_index] * pc.model` when `pc.model_mat_index >= 0`, and return `pc.model` unchanged otherwise. (Unchanged from original.)

#### Scenario: Physics-driven renderer gets composed transform
- **WHEN** `pc.model_mat_index = 2` and the solver has written the COM→GO matrix `M_com_go` at index 2, and `pc.model = M_offset`
- **THEN** the vertex shader returns `M_com_go * M_offset` as the model matrix

#### Scenario: Non-physics renderer unchanged
- **WHEN** `pc.model_mat_index = -1`
- **THEN** `get_model_matrix()` returns `pc.model` directly
