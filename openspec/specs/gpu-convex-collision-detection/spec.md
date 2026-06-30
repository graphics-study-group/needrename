# gpu-convex-collision-detection

## Purpose

Govern GPU convex collision detection using the Minkowski Portal Refinement (MPR) algorithm. Covers the collision detection compute pipeline, pair generation, MPR detection, perturbation-based contact manifold generation (with plane fitting, per-point depth, and fallback mechanisms), GPU buffer ownership, and contact margin configuration.

## Requirements

### Requirement: ConvexCollisionDetector owns GPU collision detection pipeline

The `ConvexCollisionDetector` class SHALL own a compute shader pipeline for GPU narrow-phase convex collision detection using the MPR algorithm. It SHALL follow the new detector pattern: lazy SPIR-V loading on first `Detect` call (or during `Configure`), `ComputeStage` ownership, `ComputeResourceBinding` management, and self-owned RenderGraph recording via a `Detect(vk::CommandBuffer cb)` method.

The constructor SHALL accept a `RenderSystem &` only. Sizing parameters (`max_collision_pairs`, `contact_margin`) SHALL be passed to `Configure()`. No GPU resources SHALL be allocated until `Configure` or `Detect` is first called.

The detector SHALL expose a two-phase API:
```cpp
void Configure(
    PhysicsScene &scene,
    uint32_t max_collision_pairs,
    float contact_margin,
    const ComputeBuffer &pair_buffer,
    const ComputeBuffer &pair_count_buffer
);
CollisionResultBuffers Detect(vk::CommandBuffer cb);
```

`Configure` SHALL cache `&scene`, `&pair_buffer`, and `&pair_count_buffer`; ensure internal result buffers are sized; and upload `contact_margin` to a host-visible GPU uniform buffer. `Detect` SHALL lazily build the detector's own RenderGraph, import scene buffers from the cached `PhysicsScene*` and pair buffers from cached pointers with correct `prev_access`, record passes to `cb`, and return raw `ComputeBuffer*` references to collision result buffers.

#### Scenario: Lazy initialization on first Detect call

- **WHEN** `ConvexCollisionDetector::Detect(cb)` is called for the first time after `Configure`
- **THEN** the detector loads the precompiled narrow-phase SPIR-V from `<ENGINE_PHYSICS_SPIRV_DIR>/solver/ConvexCollisionDetector/detect_collisions.comp.spv`
- **AND** creates a `ComputeStage` and `ComputeResourceBinding`
- **AND** builds its RenderGraph and records it to `cb`
- **AND** subsequent calls reuse the same pipeline and RG

#### Scenario: Missing SPIR-V produces error

- **WHEN** the collision detection SPIR-V file does not exist at runtime
- **AND** `Detect(cb)` is called
- **THEN** a `std::runtime_error` is thrown with the absolute path in the error message

#### Scenario: Detect integrates with render graph using self-imported scene buffers

- **WHEN** `Detect(cb)` is called
- **THEN** the detector creates a `RenderGraphBuilder`, calls `ImportExternalResource` for each required PhysicsScene buffer (using correct `prev_access`), imports its internal result buffers, adds clear and detect passes, builds the RG, and records it to `cb`
- **AND** the detect pass dispatches `(max_collision_pairs + 63) / 64` workgroups

### Requirement: Collision pair input buffer

`ConvexCollisionDetector` SHALL receive collision pairs to test from an external GPU buffer (`uvec2` SSBO) and pair count (`uint` SSBO), both produced by the broad-phase detector. The detector SHALL NOT own or generate the pair buffer. Each pair `(index_a, index_b)` identifies two shape indices satisfying `index_a < index_b`.

CPU dispatch SHALL use `max_collision_pairs` workgroups. Threads with `gl_GlobalInvocationID.x >= pair_count` SHALL return immediately.

#### Scenario: Collision pairs are read from external GPU buffer
- **WHEN** the collision detection shader executes
- **THEN** each invocation reads one `uvec2` from the external collision pair input buffer at `gl_GlobalInvocationID.x`
- **AND** uses the two shape indices to look up shape data from PhysicsScene buffers
- **AND** threads beyond `pair_count` return immediately

#### Scenario: Pair buffer is a render-graph resource
- **WHEN** `AddDetectPasses()` populates the render graph
- **THEN** the pair buffer and pair count buffer are imported as external resources (or passed via pre-imported handles)
- **AND** the render graph manages barrier transitions automatically

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

### Requirement: MPR collision detection algorithm

The collision detection compute shader SHALL implement the Minkowski Portal Refinement (MPR) algorithm that:
1. Discovers a tetrahedron (interior point V0 + portal triangle V1/V2/V3 on the CSO surface)
2. Validates that the origin ray from V0 intersects the portal triangle (wedge test)
3. Expands the portal outward via support queries until it converges to a CSO face
4. Determines penetration depth, contact normal, and contact points via barycentric projection of the origin onto the portal plane

The algorithm SHALL use only `support()` queries and SHALL terminate within a maximum of 32 iterations.

#### Scenario: Two overlapping boxes detected
- **WHEN** two boxes overlap in world space
- **THEN** the MPR algorithm detects penetration
- **AND** returns a contact normal pointing from B toward A (separating direction)
- **AND** penetration depth > 0

#### Scenario: Two separated boxes produce no collision
- **WHEN** two boxes are separated in world space
- **THEN** the MPR algorithm reports no collision (no overlap)
- **AND** no result is written to the output buffers

#### Scenario: Touching boxes handled
- **WHEN** two boxes are exactly touching (zero penetration depth)
- **THEN** the collision is either detected with zero depth or skipped as degenerate

### Requirement: Perturbation-based contact manifold

After MPR finds the base contact normal and point, the shader SHALL expand the contact into a manifold:

1. Compute two orthogonal axes `u, v` perpendicular to the contact normal
2. Generate 6 perturbed directions: for each 60° step around the normal, create a direction tilted 2° from the contact plane using `cos(angle)*sin(2°)*u + sin(angle)*sin(2°)*v + cos(2°)*normal`
3. For each perturbed direction, query `support()` on both shapes, collect the world-space points
4. Fit a contact plane for each shape independently using the incremental largest-triangle method from the collected world-space perturbed points (see `collision-plane-fitting` spec)
5. Project each shape's world-space perturbed points onto the 2D contact plane (u, v axes from the MPR normal), dropping the normal component
6. Apply Sutherland-Hodgman clipping: clip the projected polygon of shape A by the edges of the projected polygon of shape B
7. If the clipped polygon has > 4 vertices, apply the O(N) Rotating Calipers algorithm (see `true-rotating-calipers` spec) to select the 4 vertices forming the maximum-area quadrilateral. The algorithm SHALL find the hull diameter via antipodal walking, then find the two furthest points on opposite sides of the diameter.
8. For each selected 2D vertex, un-project to 3D by ray-casting from the MPR contact plane along the MPR normal onto the fitted plane for that shape, producing independent contact points on shapes A and B
9. Compute per-point penetration depth as `-dot(contact_point_b - contact_point_a, contact_normal)` for each pair (contact_normal points B→A, so the dot product is negative when shapes overlap); discard any point where depth <= -contact_margin (where contact_margin is a configurable uniform), and report `max(depth, 0.0f)` as the penetration for valid points
10. If the dot product of shape A's fitted plane normal and shape B's fitted plane normal is less than `cos(0.1°)`, add the MPR deepest point to the manifold. The MPR point SHALL be unconditionally appended as an additional contact point — it SHALL NOT be subjected to area-based reduction or removal.
11. If no valid contact points remain after validation (step 9) or clipping (step 6), return `point_count == 0` to indicate the MPR fallback should be used

Each valid contact point SHALL produce a separate result entry in the output buffers, with its own `contact_point_a`, `contact_point_b`, and independently computed penetration depth. Up to **5** entries per collision pair SHALL be written (4 from perturbation + optionally 1 MPR fallback).

#### Scenario: Face-to-face contact with tilted surface produces correct contact points

- **WHEN** two boxes rest face-to-face with a slight tilt (< 2°)
- **THEN** the fitted plane for each shape captures the actual surface orientation
- **AND** contact points are projected to the fitted planes (not the theoretical MPR plane)
- **AND** each contact point has its own independently computed depth
- **AND** up to 4 contact points are produced (planes aligned, no MPR fallback needed)

#### Scenario: Edge-to-edge contact produces fewer points

- **WHEN** two boxes contact along edges
- **THEN** the manifold may produce 1-2 contact points
- **AND** the points are valid world-space positions on both shapes' fitted planes

#### Scenario: Non-parallel contacts always retain MPR fallback point

- **WHEN** shape A and shape B fitted plane normals differ by more than 0.1°
- **AND** perturbation already produced 4 valid contact points
- **THEN** the MPR deepest point is appended as a 5th contact point
- **AND** the MPR point is never discarded by area optimization
- **AND** the total manifold contains exactly 5 contact points

#### Scenario: Non-parallel contacts with fewer than 4 perturbation points

- **WHEN** shape A and shape B fitted plane normals differ by more than 0.1°
- **AND** perturbation produced 2 valid contact points
- **THEN** the MPR deepest point is appended as a 3rd contact point
- **AND** no calipers reduction is applied (both counts are ≤ 5)

#### Scenario: Empty manifold falls back to MPR

- **WHEN** perturbation produces zero valid contact points (clipping failure or all points failed penetration check)
- **THEN** `perturb_manifold()` returns `point_count == 0`
- **AND** the caller uses the MPR single contact point as the collision result

### Requirement: Compute shader parallelism

The collision detection compute shader SHALL process each collision pair independently in parallel. Each workgroup invocation SHALL read one collision pair, perform MPR detection + manifold generation, and write results using atomic indexing.

The workgroup size SHALL be configurable within the shader and default to 64.

#### Scenario: Independent pair processing
- **WHEN** the collision detection shader dispatches with N workgroups
- **THEN** each workgroup processes one unique collision pair
- **AND** no inter-workgroup synchronization is required

### Requirement: Physics example integration

The `physics_example` SHALL include collision detection setup that:
1. Creates a `ConvexCollisionDetector` with `max_collision_pairs` matching the scene's all-pairs count (N*(N-1)/2)
2. Adds collision detection compute passes to the render graph via `ConvexCollisionDetector::Step()`, which internally handles GPU-side pair generation followed by collision detection
3. Runs the XPBD solver after collision detection to maintain the model matrix update pass (needed for rendering); the XPBD `step.comp` body (position modification and debug output) SHALL be commented out as a no-op placeholder

#### Scenario: Collision detection runs in example
- **WHEN** the physics example runs with collision detection enabled
- **THEN** the collision detection compute shader dispatches for each frame
- **AND** `debugPrintfEXT` outputs collision detection results to the Vulkan debug callback

#### Scenario: XPBD solver runs as no-op placeholder
- **WHEN** inspecting the physics example render graph builder
- **THEN** the XPBD Step and XPBD Model Matrix Update passes are present and dispatch
- **AND** the XPBD `step.comp` shader body is commented out (no position modification)
- **AND** the Model Matrix Update pass still runs, keeping model matrices valid for rendering

### Requirement: Debug output from collision shader

The collision detection compute shader SHALL use `debugPrintfEXT` to output detection results when the Vulkan debug extension is available. Each detected collision SHALL produce a debug message containing the shape indices, penetration depth, and contact normal.

#### Scenario: Collision debug output
- **WHEN** a collision is detected between shapes (i, j) with depth d and normal n
- **THEN** a `debugPrintfEXT` message is emitted containing i, j, d, and n
- **AND** the message appears in the Vulkan validation layer output

