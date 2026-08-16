# renderer-ancestor-rigidbody-attach Delta Spec

## MODIFIED Requirements

### Requirement: Shader composes rigid body matrix with local transform

The vertex shader's `get_model_matrix()` function in `interface.glsl` SHALL compute the final model matrix as `model_matrices.m[pc.model_mat_index] * pc.model` when `pc.model_mat_index >= 0`, and return `pc.model` unchanged otherwise.

The `model_matrices` buffer SHALL contain the **GameObject** world transform (not the COM transform). The conversion from COM transform to GO transform SHALL be performed by `model_matrix.comp` using the `rigid_body_center_offset_local_position` buffer.

#### Scenario: Physics-driven renderer gets composed transform

- **WHEN** `pc.model_mat_index = 2` and the physics solver has written the GameObject matrix `M_go` at index 2, and `pc.model = M_local`
- **THEN** the vertex shader returns `M_go * M_local` as the model matrix

#### Scenario: Non-physics renderer unchanged

- **WHEN** `pc.model_mat_index = -1`
- **THEN** `get_model_matrix()` returns `pc.model` directly (identical to current behavior)
