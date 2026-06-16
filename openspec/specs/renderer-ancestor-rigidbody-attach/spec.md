# renderer-ancestor-rigidbody-attach

## Purpose

Ensure `RendererComponent::PreRenderUpdate()` correctly finds the owning RigidBody by walking up the GameObject ancestor chain, and computes a local transform for the vertex shader to compose with the physics-driven model matrix — enabling proper visual rendering when meshes live on child GameObjects of the rigid body.

## Requirements

### Requirement: Renderer finds owning RigidBody via ancestor walk
`RendererComponent::PreRenderUpdate()` SHALL walk up the GameObject ancestor chain to find a registered RigidBody. Starting from the renderer's owning GameObject, it SHALL check each ancestor via `PhysicsScene::FindRigidBodyByObjectHandle()`. The first ancestor with a valid rigid body index SHALL be used. The walk SHALL stop at the scene root (invalid parent handle). If no rigid body is found in the chain, `model_mat_index` SHALL remain `-1`.

#### Scenario: Mesh on child GO finds RigidBody on parent
- **WHEN** a StaticMeshComponent is on "Mesh" child GO, and its parent GO has a RigidBodyComponent registered in PhysicsScene
- **THEN** `PreRenderUpdate()` finds the rigid body index from the parent GO and sets `model_mat_index` to that index

#### Scenario: Direct parent (no walk needed)
- **WHEN** a StaticMeshComponent is on the same GO as the RigidBodyComponent
- **THEN** `PreRenderUpdate()` finds the rigid body index on the immediate parent (0-level walk), preserving backward compatibility

#### Scenario: No RigidBody in ancestor chain
- **WHEN** a StaticMeshComponent's ancestor chain contains no GO with a registered RigidBody
- **THEN** `model_mat_index` remains `-1` and the push-constant model matrix is used as-is (unchanged behavior)

#### Scenario: Nearest RigidBody wins (connected block boundary)
- **WHEN** the ancestor chain contains two GOs with RigidBodyComponents (e.g., parent and grandparent)
- **THEN** the nearest one (parent) is used, matching the "connected block" boundary semantics of `CollectShapesRecursively`

### Requirement: Push-constant model carries local transform for physics-driven renderers
When a rigid body ancestor is found, the push-constant `model` matrix SHALL be set to the renderer's local transform relative to the rigid body's owning GameObject, computed as `inverse(rb_go_world_transform) * renderer_world_transform`. When no rigid body is found, the push-constant `model` matrix SHALL remain the renderer's world-space transform (unchanged behavior).

#### Scenario: Local transform computation
- **WHEN** the renderer's world transform is `M_renderer` and the rigid body GO's world transform is `M_rb`
- **THEN** the push-constant `model` is set to `inverse(M_rb) * M_renderer`

#### Scenario: Mesh at same position as rigid body GO
- **WHEN** the "Mesh" child GO has an identity local transform relative to the rigid body's GO
- **THEN** the push-constant `model` matrix is the identity matrix

### Requirement: Shader composes rigid body matrix with local transform
The vertex shader's `get_model_matrix()` function in `interface.glsl` SHALL compute the final model matrix as `model_matrices.m[pc.model_mat_index] * pc.model` when `pc.model_mat_index >= 0`, and return `pc.model` unchanged otherwise.

#### Scenario: Physics-driven renderer gets composed transform
- **WHEN** `pc.model_mat_index = 2` and the physics solver has written the center-of-mass matrix `M_com` at index 2, and `pc.model = M_local`
- **THEN** the vertex shader returns `M_com * M_local` as the model matrix

#### Scenario: Non-physics renderer unchanged
- **WHEN** `pc.model_mat_index = -1`
- **THEN** `get_model_matrix()` returns `pc.model` directly (identical to current behavior)
