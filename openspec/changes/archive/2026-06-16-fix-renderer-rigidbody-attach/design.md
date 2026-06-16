## Context

`RendererComponent::PreRenderUpdate()` currently checks only the renderer's immediate parent GameObject for a registered RigidBody via `FindRigidBodyByObjectHandle(parentObj->GetHandle())`. After `physics-scene-builder` introduced the parent-child hierarchy (RigidBody on parent GO, StaticMesh on child GO), this lookup fails — the child GO has no RigidBody registration. The result is `model_mat_index = -1`, causing the vertex shader to use the static push-constant model matrix instead of the physics-driven buffer.

`CollisionShapeComponent` already solves the same problem via `TryAttachToAncestorRigidBody()`, which walks up the ancestor chain and uses `dynamic_cast<RigidBodyComponent*>` to find the owning rigid body. The renderer should follow the same pattern.

## Goals / Non-Goals

**Goals:**
- `PreRenderUpdate()` walks the ancestor chain to find a RigidBody, matching the "connected block" semantics
- When found, the push-constant `model` matrix carries the renderer's local transform relative to the rigid body's GO
- The vertex shader composes `model_matrices.m[index] * pc.model` so per-renderer local offsets are correctly applied
- Non-physics renderers continue to work exactly as before

**Non-Goals:**
- Adding a dedicated GPU buffer for per-renderer local transforms — we reuse the existing push constant
- Changing how RigidBodyComponent collects shapes (it remains collision-only)
- Changing the XPBD solver's model matrix computation (still one matrix per rigid body center of mass)

## Decisions

### Decision 1: Ancestor walk via PhysicsScene lookup (not dynamic_cast)

**Chosen**: In `PreRenderUpdate`, for each ancestor GO, call `scene->GetGameObject(handle)` and then `physicsScene->FindRigidBodyByObjectHandle(go->GetHandle())`.

**Alternative**: Iterate components with `dynamic_cast<RigidBodyComponent*>` like `TryAttachToAncestorRigidBody`.

**Why this approach**: `PreRenderUpdate` only needs the rigid body *index*, not the component pointer. `FindRigidBodyByObjectHandle` is a simple hashmap lookup (`O(1)`) vs iterating all components on each ancestor. It also correctly handles the case where the RigidBody is registered in PhysicsScene but not yet on the component list (though in practice this shouldn't happen).

### Decision 2: Reuse push-constant for local transform (no new buffer)

**Chosen**: When `model_mat_index >= 0`, `pc.model` carries the renderer's local transform relative to the rigid body's GO. The shader computes `model_matrices.m[index] * pc.model`.

```glsl
mat4 get_model_matrix() {
    if (pc.model_mat_index >= 0) {
        return model_matrices.m[pc.model_mat_index] * pc.model;
    }
    return pc.model;
}
```

**Alternative**: New GPU buffer for per-renderer local transforms, indexed by renderer handle or a separate slot.

**Why this approach**: The push constant already exists and is uploaded per-draw. Changing its *semantics* (from world matrix to local matrix when physics-driven) costs nothing — no new buffer allocation, no descriptor binding changes, no barrier complexity. The shader change is a single `* pc.model` multiplication.

### Decision 3: Local transform = renderer_world * inv(rb_go_world)

**Chosen**: The local transform is computed as `inverse(rb_go_world_transform) * renderer_world_transform`, where `rb_go_world_transform` comes from the ancestor GO that owns the RigidBody.

This gives the renderer's pose relative to the rigid body's pivot — exactly what should be composed with the physics-updated center-of-mass matrix on the GPU.

## Risks / Trade-offs

- **[Low] push-constant semantics change**: Code that reads `pc.model` and assumes it's always a world-space matrix (e.g., for non-rendering purposes) would be affected. Mitigation: `pc.model` is only consumed by vertex shaders via `get_model_matrix()`, and the new behavior is the correct one.
- **[Low] Ancestor walk depth**: In pathological deep trees (hundreds of levels), the walk could be slow. Mitigation: Game object trees are typically flat (<10 levels). The `FindRigidBodyByObjectHandle` lookup is O(1) per level.
- **[Low] Multiple rigid bodies in ancestor chain**: If there are two RigidBodyComponent GOs in the chain (e.g., parent AND grandparent), the algorithm stops at the nearest one (child). This matches "connected block" semantics — a child RigidBody defines a new block boundary.
