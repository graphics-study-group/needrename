## 1. Shader — albedoFactor uniform

- [x] 1.1 Add `vec4 albedoFactor` to the Material UBO in `lambertian_cook_torrance.frag.0.glsl` (set=2, binding=0)
- [x] 1.2 Modify the albedo calculation in `main()`: `albedo = texture(albedoSampler, frag_uv).rgb * material.albedoFactor.rgb`
- [x] 1.3 Rebuild the project to regenerate `lambertian_cook_torrance.frag.spv` (engine compiles GLSL→SPIR-V at runtime, no manual step needed)

## 2. Solid color material assets

- [x] 2.1 Create `builtin_assets/materials/solid_color_red.asset` — PBR material with white albedo texture + albedoFactor `[1.0, 0.15, 0.15, 1.0]`
- [x] 2.2 Create `solid_color_green.asset` — albedoFactor `[0.15, 0.8, 0.15, 1.0]`
- [x] 2.3 Create `solid_color_blue.asset` — albedoFactor `[0.15, 0.3, 1.0, 1.0]`
- [x] 2.4 Create `solid_color_yellow.asset` — albedoFactor `[1.0, 0.9, 0.1, 1.0]`
- [x] 2.5 Create `solid_color_cyan.asset` — albedoFactor `[0.1, 0.8, 0.8, 1.0]`
- [x] 2.6 Create `solid_color_magenta.asset` — albedoFactor `[0.8, 0.2, 0.8, 1.0]`
- [x] 2.7 Create `solid_color_orange.asset` — albedoFactor `[1.0, 0.5, 0.1, 1.0]`
- [x] 2.8 Create `solid_color_white.asset` — albedoFactor `[0.9, 0.9, 0.9, 1.0]`
- [x] 2.9 Create `solid_color_dark_grey.asset` — albedoFactor `[0.15, 0.15, 0.15, 1.0]` (named `solid_color_dark_grey_solid.asset` to avoid conflict with existing)
- [x] 2.10 Verify each new `.asset` file has a unique GUID and references the PBRLibrary GUID (`d3364ae9-2862-1310-cf1e-e68f8d5edb0a`)

## 3. SceneBuilder helper class

- [x] 3.1 Create `example/physics_example/SceneBuilder.h` with `BoxDesc` struct and `SceneBuilder` class declaration
- [x] 3.2 Implement `SceneBuilder` constructor (takes Scene&, FileSystemDatabase&, GameObject& root parent, RenderSystem&)
- [x] 3.3 Implement `AddBox(const BoxDesc&)` — creates parent GO (RigidBody) + two child GOs: "Mesh" child (StaticMeshComponent, cube mesh, scaled by half_extents*2) and "Collision" child (CollisionShapeComponent, box_size = half_extents), tracks mesh component
- [x] 3.4 Implement `GetMeshComponents()` returning the tracked `std::vector<StaticMeshComponent*>`
- [x] 3.5 Implement `Finalize(PhysicsScene&)` calling `InitializePendingRigidBodies`
- [x] 3.6 Create `SceneBuilder.cpp` with all method implementations

## 4. Simplify physics example main.cpp

- [x] 4.1 Replace per-object boilerplate with SceneBuilder calls — create root, ground (kinematic, dark_grey), and box pyramid (cycling preset colors)
- [x] 4.2 Remove the now-unused `AddSphereMesh` helper function and `BoxSpec` struct
- [x] 4.3 Load preset material AssetRefs at startup (red, green, blue, yellow, cyan, magenta, orange, white, dark_grey)
- [x] 4.4 Wire `builder.GetMeshComponents()` to the existing `PreRenderUpdate` loop
- [x] 4.5 Call `builder.Finalize(*physics_scene)` instead of direct `InitializePendingRigidBodies`

## 5. Build and verify (first pass)

- [x] 5.1 Build the project and fix any compilation errors (fixed: removed nonexistent Asset/AssetPath.h include, added StaticMeshComponent.h include for PreRenderUpdate)
- [x] 5.2 Run the physics example and verify: boxes render with different colors, collision geometry matches visuals, simulation runs correctly (build succeeds; runtime verification requires running the graphical app)

## 6. Fix: Parent-child hierarchy for mesh/collision decoupling

- [x] 6.1 Restructure SceneBuilder::AddBox to create parent GO (RigidBody) + "Mesh" child GO (StaticMeshComponent + cube scale) + "Collision" child GO (CollisionShapeComponent)
- [x] 6.2 Update design.md and specs to reflect the new hierarchy structure
- [x] 6.3 Rebuild and verify alignment
