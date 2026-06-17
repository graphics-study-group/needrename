## Context

The GPU physics framework currently consists of:
- **PhysicsScene**: Owns rigid-body and collision-shape data in SoA layout, mirrored to GPU SSBOs
- **XPBDGpuSolver**: Loads precompiled SPIR-V, creates `ComputeStage` + `ComputeResourceBinding`, and adds compute passes to the render graph via `RenderGraphBuilder`
- **No collision detection** of any kind exists

The compute shader pipeline (CMake → glslangValidator → SPIR-V → runtime loading) automatically discovers all `.comp` files under `engine/Physics/shader/`. All physics shaders use `#version 450 core` with Vulkan descriptor-set bindings.

The render graph pattern is: import external `ComputeBuffer` resources → `UseBuffer()` with access masks → `AddPass()` with a `SetPassFunction` lambda that binds the `ComputeStage`, binds the `ComputeResourceBinding`, and dispatches.

## Goals / Non-Goals

**Goals:**
- GPU narrow-phase convex collision detection using MPR algorithm
- Support-function abstraction — new convex shapes need only a `support()` function
- Box support function as the first concrete implementation
- Contact manifold generation: perturbation method → 2D clipping → 4-point reduction
- Collision result buffers (collision pair IDs, normals, contact points) as separate SSBOs
- Collision count via GPU atomic counter
- All-pairs collision pair generation (n*(n-1)/2) on GPU via a dedicated pair-generation compute shader, avoiding CPU-GPU transfer and synchronization overhead
- `ConvexCollisionDetector` class following the `XPBDGpuSolver` pattern: lazy SPIR-V loading, `ComputeStage` ownership, render-graph integration
- Test in `physics_example` with XPBD solver passes commented out
- Debug output via `debugPrintfEXT`

**Non-Goals:**
- Broad phase (SAP, BVH, grid, etc.) — all-pairs brute force is the temporary substitute
- Collision response / contact resolution — this only detects and reports collisions
- Shapes other than Box — the support-function interface is designed for extensibility but only box is implemented
- Continuous collision detection (CCD) — only discrete detection at current frame poses
- Performance optimization — correctness and readability first
- Integration with the XPBD solver — that comes in a future change (current pipeline: collision detection → XPBD no-op passes → model matrix update → rendering)

## Decisions

### Decision 1: ConvexCollisionDetector follows XPBDGpuSolver pattern

**Choice**: Own class with lazy SPIR-V loading, `ComputeStage` ownership, `Step()` that populates a `RenderGraphBuilder`.

**Rationale**: Proven pattern in the codebase. The `XPBDGpuSolver` demonstrates correct Vulkan resource lifecycle management. Deviating would risk descriptor-set or pipeline-lifetime bugs.

**Alternatives considered**:
- Embed in PhysicsScene: Would bloat PhysicsScene's responsibilities (it already owns 23 buffers + all CPU data). Collision detection is a separate concern.
- Pure-CPU implementation: Would require GPU→CPU readback, defeating the purpose of GPU physics.

### Decision 2: Separate SSBO per result type (SoA layout)

**Choice**: `collision_pair_ids` (uvec2), `collision_normals` (vec4), `contact_point_a` (vec4), `contact_point_b` (vec4), plus `collision_count` (uint atomic). Each in its own buffer.

**Rationale**: Cache-friendly access. The solver consuming these results will read normals for all contacts (not points), or points for all contacts (not normals). SoA avoids loading unused data into cache lines. Also matches PhysicsScene's existing buffer organization.

**Alternatives considered**:
- Interleaved struct (AoS): Would be simpler to write but worse for GPU cache when only some fields are needed.
- Single large buffer with offset regions: Complicates binding and size management.

### Decision 3: All-pairs generation on GPU via dedicated compute shader

**Choice**: Generate n*(n-1)/2 pairs on GPU using a dedicated pair-generation compute shader (`generate_pairs.comp`). The shader dispatches `max_pairs` threads; each thread computes its `(i, j)` pair from `gl_GlobalInvocationID.x` using the upper-triangle index formula and writes it to a `uvec2` SSBO. The collision detection shader then reads this buffer.

**Rationale**: Keeping pair generation entirely on GPU avoids a CPU→GPU transfer every frame. While the data volume for all-pairs is modest (~21 pairs × 8 bytes = 168 bytes for the test scene), the transfer requires a staging buffer submission and a pipeline barrier, adding synchronization complexity to the render graph. A GPU-side generation pass is a simple compute dispatch that chains naturally with the collision detection pass — both live in the same render graph, with the barrier managed automatically by `UseBuffer` read/write declarations. Additionally, when broad phase is added later, only the pair-generation shader changes (to a broad-phase shader); the collision detection shader's input interface stays identical.

**Alternatives considered**:
- CPU-side generation + upload: Requires `EnqueueBufferSubmission` + `ExecuteSubmissionImmediately`, adding a CPU-GPU sync point. Rejected per user guidance.
- Fusing pair index computation directly into the collision detection shader (each thread computes its own pair from `gl_GlobalInvocationID.x`, no separate pair buffer): Eliminates the pair buffer entirely but couples the index mapping to the collision shader. Keeping the pair buffer as an intermediate output provides a clean interface for future broad-phase replacement.

### Decision 4: MPR algorithm implementation in compute shader

**Choice**: Implement Minkowski Portal Refinement in a single compute shader with shared GLSL headers.

**Rationale**: MPR is well-suited for GPU: it's iterative but converges quickly (typically < 10 iterations), uses only support-function queries (no face/edge traversal), and handles penetrating and separated cases uniformly. It finds a single contact point and normal, which the perturbation method then expands into a manifold.

**Alternatives considered**:
- GJK + EPA: GJK finds closest points for separated objects; EPA expands for penetration. Both are more complex and require more state. MPR combines both in one algorithm.
- SAT (Separating Axis Theorem): Works well for boxes/polytopes but doesn't generalize to arbitrary convex hulls via support functions.

### Decision 5: Perturbation method for contact manifold

**Choice**: After MPR finds the base contact normal:
1. Compute two orthogonal axes `u, v` perpendicular to the normal
2. Generate 6 perturbed directions (every 60° around the normal, tilted 2° off the contact plane)
3. For each direction, get support points from both shapes, project onto the 2D contact plane
4. Sutherland-Hodgman clipping: clip polygon A by edges of polygon B (variant for overlapping polygons)
5. If result has > 4 vertices, reduce to 4 using rotating calipers for maximum quadrilateral area

**Rationale**: Edge-sampling methods (like EPA) can miss thin features. Perturbation stably samples the contact region from slightly different directions, producing a robust manifold. The 6-sample/2° tilt parameters are standard values from physics engines like Bullet.

**Alternatives considered**:
- Clipping two faces directly: Requires face adjacency info, which the support-function interface doesn't provide.
- Simple closest-points: Insufficient for stable stacking — single-point contacts lead to jitter.

### Decision 6: Buffer maximums set at construction time

**Choice**: `ConvexCollisionDetector` constructor takes `max_collision_pairs` and pre-allocates all result buffers at `max_collision_pairs * 4` entries. Each manifold contact point is a separate result entry (up to 4 per collision pair), atomically reserved via a single `atomicAdd(count, num_points)`. The `collision_count` SSBO is a single `uint`.

**Rationale**: Each manifold point is a standalone result with its own contact position, simplifying downstream consumption. The `atomicAdd(N)` pattern reserves N contiguous slots in one atomic operation, avoiding per-point atomic contention. The *4 multiplier ensures sufficient capacity even if every pair produces a full 4-point manifold.

### Decision 7: GLSL code organization with header files

**Choice**: Two compute shaders with shared GLSL headers:
- `generate_pairs.comp` — GPU-side all-pairs generation (no includes needed, self-contained)
- `detect_collisions.comp` — Main collision detection entry point, with `#include` for:
  - `mpr.glsl` — MPR algorithm implementation
  - `support.glsl` — Support function interface + box implementation
  - `perturbation.glsl` — Perturbation manifold generation
  - `clipping.glsl` — Sutherland-Hodgman 2D clipping

**Alternative**: Monolithic shader. Rejected for readability and testability — the user explicitly requested multiple header files.

### Decision 8: Collision pair indexing scheme

**Choice**: Collision pairs are stored in a `uvec2` buffer where `pair.x = index_a`, `pair.y = index_b`, with `index_a < index_b` (upper triangle only). For rigid bodies with multiple shapes, pairs enumerate shape indices, not rigid body indices.

**Rationale**: Shape-level collision is needed because a rigid body may have multiple shapes. The shape indices map back to rigid bodies via `shape_bound_rigid_body`.

## Risks / Trade-offs

- **All-pairs brute force is O(n²)**: For small test scenes (~10 objects) this is fine. Scaling to hundreds of objects will require broad phase. → Mitigation: The collision pair buffer is an input parameter — when broad phase is added, it simply replaces the CPU-generated all-pairs with a GPU-generated sparse set.

- **MPR convergence in edge cases**: Touching edges or coplanar faces can cause MPR to loop. → Mitigation: Hard iteration limit (e.g., 32) with early-out for degenerate cases. Degenerate contacts (zero depth) are skipped.

- **Perturbation method only outputs up to 4 contact points**: Complex contacts (e.g., box resting perfectly on another box) ideally need 4 points, which is the max this method produces. → Mitigation: 4 is standard for stable stacking; more points are rarely needed for rigid body simulation.

- **GLSL `#include` may not work with glslangValidator**: glslangValidator supports `#include` via the `GL_GOOGLE_include_directive` extension — resolved by adding `#extension GL_GOOGLE_include_directive : require` to the main shader. glslangValidator resolves `#include "..."` relative to the source file's directory by default, so no CMake `-I` flag changes were needed.

- **Separate SSBOs double the binding count**: 5 output buffers + 1 count buffer + 1 input buffer = 7 new bindings on top of PhysicsScene's 23 buffers. → Mitigation: Vulkan descriptor sets can handle this comfortably (minimum 1024 bindings per set). The descriptor set layout reflection in `ComputeStage` handles this automatically.

## Open Questions

- Should the contact point positions be in world space or local space? **Resolved**: World space — simpler for the consumer (solver). Support functions already operate in world space, making this natural.
- Should `glslangValidator` `#include` paths be configured via CMake `-I` flags or via a manual concatenation step? **Resolved**: `GL_GOOGLE_include_directive` extension with source-relative `#include "..."` — no CMake changes needed.
