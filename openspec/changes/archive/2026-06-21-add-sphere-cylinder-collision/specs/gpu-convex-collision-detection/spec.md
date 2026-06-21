# gpu-convex-collision-detection — Delta Spec

## MODIFIED Requirements

### Requirement: Support function interface for convex shapes

The collision detection compute shader SHALL define a GLSL function `vec3 support(uint shape_index, vec3 direction)` that returns the world-space support point (farthest point in the given direction) for the shape identified by `shape_index`. The function SHALL dispatch on `shape_type.v[shape_index]` using the following type constants:

- `SHAPE_TYPE_BOX = 0u`: calls `support_box(feature.xyz, world_pos, world_rot, dir_world)` — transforms direction to local space, computes `sign(dot) * half_extents` per axis, transforms back
- `SHAPE_TYPE_SPHERE = 1u`: calls `support_sphere(feature, world_pos, world_rot, dir_world)` — returns `world_pos + normalize(dir_world) * feature.x`
- `SHAPE_TYPE_CYLINDER = 2u`: calls `support_cylinder(feature, world_pos, world_rot, dir_world)` — transforms direction to local space, decomposes into Z (axial) and XY (radial) components, computes `z_sign * feature.y` + `normalize(dir_xy) * feature.x`

The feature payload SHALL be read from `shape_feature.v[shape_index].xyz` (formerly `shape_half_extents`).

Unknown shape types SHALL return `world_pos` as a fallback support point.

#### Scenario: Box support returns correct farthest point
- **WHEN** `support(box_index, vec3(1,0,0))` is called for a box at world origin with feature (2, 1, 0.5) and identity rotation
- **THEN** the returned point is `(2, 0, 0)` (the right face center)

#### Scenario: Box support with rotation
- **WHEN** `support(box_index, direction)` is called for a rotated box
- **THEN** the direction is inversely rotated to box local space
- **AND** the support point is computed in local space
- **AND** the result is rotated back to world space and translated by world position

#### Scenario: Sphere support is rotationally invariant
- **WHEN** `support(sphere_index, direction)` is called for a rotated sphere at world position `(5,0,0)` with radius 2.0
- **THEN** the returned point is `(5,0,0) + normalize(direction) * 2.0`
- **AND** the sphere's rotation quaternion has no effect on the result

#### Scenario: Cylinder support along axial direction
- **WHEN** `support(cylinder_index, vec3(0,0,1))` is called for a Z-up cylinder at origin with feature (1.0, 0.5, 0) and identity rotation
- **THEN** the returned point is `(0, 0, 0.5)` — the center of the top face

#### Scenario: Cylinder support along radial direction
- **WHEN** `support(cylinder_index, vec3(1,0,0))` is called for a Z-up cylinder at origin with feature (1.0, 0.5, 0) and identity rotation
- **THEN** the returned point is `(1.0, 0, 0)` — the farthest point on the side

#### Scenario: Unknown shape type returns world position fallback
- **WHEN** `support(shape_index, dir)` is called for a shape with an unrecognized type value
- **THEN** the function returns `world_pos` (the shape's world position)
