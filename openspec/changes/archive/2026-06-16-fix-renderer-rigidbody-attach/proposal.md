## Why

`RendererComponent::PreRenderUpdate()` only checks the renderer's immediate parent GameObject for a registered RigidBody — it does not walk up the ancestor chain. When the render mesh lives on a child GameObject (as introduced by the `physics-scene-builder` parent-child hierarchy), `model_mat_index` stays at `-1`, and the vertex shader falls back to the static push-constant model matrix instead of using the physics-driven `model_matrices` buffer. Additionally, there is no mechanism to pass the renderer's local transform relative to the rigid body's GameObject to the shader, which is needed for correct per-shape/mesh offsets in multi-component rigid bodies.

## What Changes

- **Ancestor walk in PreRenderUpdate**: `RendererComponent::PreRenderUpdate()` SHALL walk up the GameObject ancestor chain (analogous to `CollisionShapeComponent::TryAttachToAncestorRigidBody()`) to find any ancestor GameObject with a registered RigidBody in PhysicsScene
- **Local transform for physics-driven renderers**: When a rigid body ancestor is found, the push-constant `model` matrix SHALL carry the renderer's local transform relative to the rigid body's owning GameObject (computed as `renderer_world * inverse(rb_go_world)`)
- **Shader composes transforms**: The vertex shader's `get_model_matrix()` SHALL compose the rigid body's center-of-mass model matrix with the renderer's local transform: `model_matrices.m[index] * pc.model` instead of merely returning the buffer entry
- **Non-physics renderers unchanged**: When no rigid body ancestor is found, behavior is identical to current (`model_mat_index = -1`, `pc.model` = world transform)

## Capabilities

### New Capabilities
- `renderer-ancestor-rigidbody-attach`: RendererComponent SHALL find its owning RigidBody by walking up the ancestor chain, and SHALL compute and transmit the renderer's local transform relative to the rigid body's GameObject so the vertex shader can compose the correct world-space model matrix

### Modified Capabilities
<!-- None — new capability only -->

## Impact

- **Engine**: `engine/Framework/component/RenderComponent/RendererComponent.cpp` — `PreRenderUpdate()` logic change
- **Shader**: `builtin_assets/shaders/include/engine/interface.glsl` — `get_model_matrix()` change (1 line)
- **No new GPU buffers needed**: reuses existing push-constant `pc.model` for the local transform
- **Backward compatible**: non-physics renderers (`model_mat_index = -1`) are completely unaffected
