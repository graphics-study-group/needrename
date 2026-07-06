## MODIFIED Requirements

### Requirement: Collision result GPU buffers

`ConvexCollisionDetector` SHALL own and expose GPU output buffers for collision results. Each manifold contact point is a separate result entry, so all buffers are sized to `max_collision_pairs * 5` (up to 5 points per collision pair):

- `collision_ids`: `uvec2` buffer storing `(shape_index_a, shape_index_b)` for each contact point
- `collision_normals`: `vec4` buffer storing the contact normal in world space (w = independently computed penetration depth per point)
- `contact_point_a`: `vec4` buffer storing the contact point on shape A in **shape-local space** (relative to shape A's local frame, not world space)
- `contact_point_b`: `vec4` buffer storing the contact point on shape B in **shape-local space** (relative to shape B's local frame, not world space)
- `collision_count`: single `uint` buffer, atomically incremented to reserve slots

All buffers SHALL be separate SSBOs (SoA layout) for cache-friendly access.

The collision detection compute shader SHALL convert world-space contact points to shape-local space before writing, using the shape's world pose at detection time:
```glsl
local_pt_a = quat_inv_rotate(shape_world_rot[shape_a], world_pt_a - shape_world_pos[shape_a]);
local_pt_b = quat_inv_rotate(shape_world_rot[shape_b], world_pt_b - shape_world_pos[shape_b]);
```

#### Scenario: Collision count starts at zero

- **WHEN** a collision detection pass begins
- **THEN** the `collision_count` buffer is reset to 0 before dispatch

#### Scenario: Results written per manifold point, up to 5 per pair

- **WHEN** a compute thread detects a collision and produces N manifold points (1-5)
- **THEN** it atomically adds N to `collision_count` to reserve N contiguous slots
- **AND** writes each point as a separate entry with its own contact positions (in shape-local space) and independently computed penetration depth
- **AND** if any slot exceeds the buffer capacity, the write is skipped

#### Scenario: Contact points are in shape-local space

- **WHEN** collision detection outputs a contact point for shape A at world position `(5, 2, 1)`
- **AND** shape A has world position `(3, 2, 1)` and identity rotation
- **THEN** `contact_point_a` stores `(2, 0, 0)` (the offset from shape origin in shape-local space)

#### Scenario: Contact points account for shape rotation

- **WHEN** collision detection outputs a contact point for shape A at world position `(5, 2, 1)`
- **AND** shape A has world position `(3, 2, 1)` and is rotated 90° around Z
- **THEN** `contact_point_a` stores the world offset rotated by -90° around Z (inverse shape rotation)
