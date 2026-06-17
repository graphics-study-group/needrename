## Why

The GPU physics framework currently has no collision detection — the XPBD solver is a placeholder that only sinks objects along Z. To build a real physics simulation, we need narrow-phase collision detection that runs entirely on the GPU. This change adds GPU-accelerated convex hull collision detection using the MPR (Minkowski Portal Refinement) algorithm with contact manifold generation via the perturbation method, laying the foundation for future collision response.

## What Changes

- New `ConvexCollisionDetector` class that owns compute shader pipelines, result buffers, and dispatch logic
- New compute shader files for GPU-side pair generation (`generate_pairs.comp`) and MPR-based convex collision detection (`detect_collisions.comp`), organized with shared GLSL headers for readability
- A `support` function interface in GLSL, with box support as the first concrete implementation — designed to be extensible to other convex shapes
- Contact manifold generation using the perturbation method (MPR → perturb normal → 2D clipping → rotating calipers reduction)
- GPU-side all-pairs collision pair generation (n*(n-1)/2) via a dedicated pair-generation compute shader, avoiding CPU-GPU transfer overhead
- GPU result buffers: collision pair IDs, contact normals (vec4), contact points on obj1 (world), contact points on obj2 (world), plus a collision count atomic; all organized as separate buffers for cache-friendly access
- Integration into `physics_example` with a simple test scene, temporarily commenting out the XPBD solver steps
- Debug output from the collision compute shader via `debugPrintfEXT` for verification

## Capabilities

### New Capabilities

- `gpu-convex-collision-detection`: GPU narrow-phase convex collision detection using MPR algorithm with support-function-based shape abstraction, perturbation-based contact manifold generation, compute-shader parallel dispatch per collision pair, and structured output buffers for collision results. Covers the `ConvexCollisionDetector` class, its compute shaders, and the all-pairs input generation.

### Modified Capabilities

- `physics-gpu-shaders`: New shader files under `engine/Physics/shader/solver/ConvexCollisionDetector/` extend the physics shader set. The existing CMake GLSL→SPIR-V pipeline automatically picks them up without modification.

## Impact

- **New files**: `engine/Physics/Collision/ConvexCollisionDetector.h/.cpp`, compute shaders under `engine/Physics/shader/solver/ConvexCollisionDetector/` (multiple `.comp` and `.glsl` header files)
- **Modified files**: `example/physics_example/main.cpp` (add collision detection setup, comment out XPBD), `example/physics_example/PhysicsExampleRenderGraphBuilder.h/.cpp` (add collision passes), `example/physics_example/CMakeLists.txt` (if needed for new source files)
- **Existing specs**: `physics-gpu-shaders` spec gains new scenarios for collision detector shaders
- **No API breakage**: All existing code paths are preserved; XPBD solver is only commented out in the example, not removed
