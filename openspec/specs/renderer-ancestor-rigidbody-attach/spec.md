# renderer-ancestor-rigidbody-attach

## Purpose

Ensure `RendererComponent::PreRenderUpdate()` correctly finds the owning RigidBody by walking up the GameObject ancestor chain, and composes the renderer's local transform with the physics-driven model matrix — including GO-local COM offset compensation — enabling proper visual rendering when meshes live on child GameObjects of the rigid body.

## Requirements

### Requirement: Renderer finds owning RigidBody via ancestor walk

`RendererComponent::PreRenderUpdate()` SHALL, when physics is active (`PhysicsAdaptor::IsPhysicsActive()`), walk up the GameObject ancestor chain to find a registered RigidBody. Starting from the renderer's owning GameObject, it SHALL check each ancestor via `PhysicsAdaptor::FindRigidBodyByObjectHandle()`. The first ancestor with a valid rigid body index SHALL be used. The walk SHALL stop at the scene root (invalid parent handle). If no rigid body is found in the chain, `model_mat_index` SHALL remain `-1`.

#### Scenario: Mesh on child GO finds RigidBody on parent

- **WHEN** a StaticMeshComponent is on "Mesh" child GO, and its parent GO has a RigidBodyComponent registered through the physics adaptor
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

#### Scenario: Physics inactive falls back to world transform

- **WHEN** `PhysicsAdaptor::IsPhysicsActive()` is `false`
- **THEN** no ancestor walk is performed, `model_mat_index` remains `-1`, and the renderer uses its world transform directly

### Requirement: Push-constant model carries local transform with COM offset compensation

When a rigid body ancestor is found, the push-constant `model` matrix SHALL be computed as `translate(-com_offset) * inverse(rb_world) * renderer_world`, where `com_offset = PhysicsAdaptor::GetComOffsetLocal(rigid_idx)` and `rb_world` is the rigid body GameObject's world transform with scale removed. When no rigid body is found, the push-constant `model` matrix SHALL remain the renderer's world-space transform (unchanged behavior).

#### Scenario: Local transform computation

- **WHEN** the renderer's world transform is `M_renderer` and the rigid body GO's world transform is `M_rb`
- **THEN** the push-constant `model` is set to `translate(-com_offset) * inverse(M_rb) * M_renderer`

#### Scenario: Mesh at same position as rigid body GO with zero COM offset

- **WHEN** the "Mesh" child GO has an identity local transform relative to the rigid body's GO and `com_offset = (0,0,0)`
- **THEN** the push-constant `model` matrix is the identity matrix

#### Scenario: Non-zero COM offset shifts rendered mesh to GO origin

- **WHEN** `com_offset = (2, 0, 0)` and the rb world transform is identity
- **THEN** the push-constant `model` matrix contains a translation of `(-2, 0, 0)` (mesh rendered at GO origin while the body is simulated around its COM)

### Requirement: Shader composes rigid body matrix with local transform

The vertex shader's `get_model_matrix()` function in `interface.glsl` SHALL compute the final model matrix as `model_matrices.m[pc.model_mat_index] * pc.model` when `pc.model_mat_index >= 0`, and return `pc.model` unchanged otherwise.

The `model_matrices` buffer SHALL contain the rigid body's **COM** world transform as written by `model_matrix.comp` (no center-offset input). GO-position compensation SHALL be applied CPU-side in `RendererComponent::PreRenderUpdate` via the COM offset term of the push-constant `model` matrix.

#### Scenario: Physics-driven renderer gets composed transform

- **WHEN** `pc.model_mat_index = 2` and the physics solver has written the COM matrix `M_com` at index 2, and `pc.model = M_local` (already COM-offset compensated)
- **THEN** the vertex shader returns `M_com * M_local` as the model matrix

#### Scenario: Non-physics renderer unchanged

- **WHEN** `pc.model_mat_index = -1`
- **THEN** `get_model_matrix()` returns `pc.model` directly (identical to current behavior)
