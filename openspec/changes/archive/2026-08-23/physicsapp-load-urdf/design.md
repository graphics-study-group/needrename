# Design: PhysicsApp URDF Loading

## Context

Two disconnected paths exist today:

- `UrdfLoader::LoadUrdfResource(path, path_in_project)` (Framework): parses URDF XML, builds a GameObject hierarchy into a **temporary scene** obtained via `MainClass::GetInstance()->GetWorldSystem()->CreateScene()`, then `SaveFromScene()` + `SaveAsset()` persists a `SceneAsset` prefab. The hierarchy builder `BuildAndSaveSceneAsset` mixes building (steps 1–7) with persistence (step 8).
- `PhysicsApp` (AppPhysics): builds bodies manually via `SceneBuilder` (`AddBox`/`AddSphere`/`AddCylinder`), registering each parent GameObject into `m_bodies` so `BodyId = index`. `BuildPhysicsReadback` maps each `BodyId` → rigid-body slot via `PhysicsAdaptor::FindRigidBodyByObjectHandle`, making every registered body readable through `GetBodyState`/`GetBodyStates`.

`PhysicsApp` will be packaged as a library. URDF robots must be loadable into its running scene without writing asset files; the returned name→`BodyId` maps are the prerequisite for the future force-driving work (explicitly out of scope here).

Key facts verified in code:

- `UrdfLoader` already holds `m_asset_manager`/`m_database` from `GetAssetRuntime()` (`UrdfLoader.cpp:590-594`); the visual step (step 7) is the only consumer of `db` (builtin mesh/material resolution). Steps 2–6 (physics) need no asset system.
- `ObjectHandle` has a `std::hash` specialization (`Framework/World/Handle.h:142-148`), so `unordered_map<ObjectHandle, BodyId>` works out of the box.
- `PhysicsApp` already links `Engine` (`app/physics/CMakeLists.txt:7`), so calling `UrdfLoader` needs no CMake change.
- `GetHandle()` is valid immediately after `CreateGameObject()` (existing `SceneBuilder::RegisterBody` relies on it before any flush).
- A1 facts: `base` and `*_thigh_shoulder` links have no `<inertial>` (no rigid body); `floating_base` and `*_hip_fixed` joints therefore produce no constraint (skipped today); all visuals are mesh (`*.dae`) and get skipped in Phase 1, so the robot renders from its collision geometry.

## Goals / Non-Goals

**Goals:**
- A reusable, public `UrdfLoader::BuildRobotScene` that builds into a caller-provided scene without flushing or saving, returning link/joint → handle maps.
- `PhysicsApp::LoadUrdf(config)` returning link/joint → `BodyId` maps, fully integrated with the existing readback APIs.
- Configurable overall placement (position/rotation) and uniform friction/restitution; visuals switch.
- A windowed test scene loading the A1 robot alongside the existing template scene.
- The existing save-to-disk path keeps its exact behavior, reimplemented on top of the extracted builder.

**Non-Goals:**
- Joint driving / force application APIs (deferred to a future unified rigid-body-mutation design).
- Mesh (`dae`/`stl`) visual import (existing Phase 2 gap, unchanged).
- Fixing/anchoring the root link (the robot free-falls; the test ground catches it).
- Any change to `LoadUrdfResource`'s public signature or output files.

## Decisions

### D1: Extract `BuildRobotScene` as the single builder, wrap the save path

All hierarchy construction (current steps 1–7) moves into `UrdfLoader::BuildRobotScene`, which builds into a caller-provided `Scene`. `BuildAndSaveSceneAsset` becomes: create temporary scene → `BuildRobotScene` (root_parent = null, default options, discard returned maps) → `FlushCmdQueue` → `SaveFromScene` → `SaveAsset`.

- **Alternative rejected: duplicate the build logic in AppPhysics.** Two copies of ~200 lines of coordinate conversion, inertia rotation, collision filtering and visual assembly would drift.
- **Alternative rejected: keep two Framework builders (one saving, one not).** Same drift risk; the map collection is cheap ("collect while building").

### D2: `BuildRobotScene` uses member `m_database`/`m_asset_manager`, not parameters

`BuildRobotScene(robot, scene, root_parent, options)` uses the members populated in the constructor. The existing `BuildAndSaveSceneAsset(robot, path, am, db)` parameter style is historical; the parameters duplicate the members anyway (`LoadUrdfResource` extracts them from the members). The wrapper keeps calling `CreateAsset<SceneAsset>` via the member for the save path.

- **Alternative rejected: pass `db` as a parameter.** Redundant for `PhysicsApp` (it constructs an `UrdfLoader` whose members are already seeded) and would force `PhysicsApp` to re-obtain the database.
- **Consequence:** `ParseUrdf` becomes public (parse and build remain two steps so callers may edit the intermediate representation before building).

### D3: Two maps with strict "physics-realized only" semantics

`UrdfBuiltRobot` collects:
- `link_objects[name] = handle` only when the link got a `RigidBodyComponent` (has `<inertial>`);
- `joint_objects[name] = {parent_handle, child_handle}` only when a constraint was actually created (both ends have rigid bodies, type fixed/revolute/continuous).

Skipped links/joints simply do not appear — callers use `find()` to test presence. No `INVALID_BODY_ID` placeholders (user decision).

- **Rationale:** maps are only useful for physics interaction; entries without bodies have no `BodyId` to offer, and placeholder values force every consumer to guard anyway.

### D4: `JointBodyPair { parent, child }` follows URDF semantics

`parent` is the link closer to the root. This deliberately hides the engine-internal fact that the constraint component's owner (obj1) is the child link and `m_obj2_handle` points at the parent (`UrdfLoader.cpp:459-472`).

- **Rationale:** future driving APIs (torque on a joint) need "anchor end" vs "driven end"; URDF parent/child is the language robotics users already speak. Internal ownership stays an implementation detail.

### D5: Options struct with placement + coefficients + visuals switch

```cpp
// Framework (UrdfTypes.h)
struct UrdfBuildOptions {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float static_friction{0.5f};
    float dynamic_friction{0.5f};
    float restitution{0.0f};
    bool with_visuals{true};
};
```

`position`/`rotation` are applied to the root link's GO (which is parented to `root_parent` when non-null); the coefficients are written to every `RigidBodyComponent` (today the URDF builder never touches these fields). `with_visuals=false` skips step 7 entirely, mirroring `SceneBuilder`'s `with_visuals`. In `PhysicsApp`, visuals are driven by the app mode (`mode != PhysicsOnly`) rather than a config field, so `UrdfImportConfig` does not expose a `with_visuals` switch.

- **Note:** the root link GO may itself have no rigid body (A1 `base`) — the placement still takes effect because the whole link tree hangs off it.

### D6: `PhysicsApp::LoadUrdf` flow

```cpp
UrdfImportResult PhysicsApp::LoadUrdf(const UrdfImportConfig &cfg) {
    // phase guard: logic_error after CommitScene
    // file existence + parse: UrdfLoader loader; UrdfRobot robot = loader.ParseUrdf(cfg.urdf_path);
    //   robot.links.empty() -> runtime_error
    UrdfBuildOptions opts{...};               // from cfg
    opts.with_visuals = mode != PhysicsOnly;  // rendered modes build visuals
    UrdfBuiltRobot built = loader.BuildRobotScene(robot, *scene, *root, opts);

    std::unordered_map<ObjectHandle, BodyId> handle_to_body;   // std::hash exists
    for (auto &[name, handle] : built.link_objects) {
        BodyId id = builder->RegisterExistingBody(handle);
        result.link_bodies[name] = id;
        handle_to_body[handle] = id;
    }
    for (auto &[name, pair] : built.joint_objects)
        result.joint_bodies[name] = {handle_to_body.at(pair.parent), handle_to_body.at(pair.child)};
    return result;
}
```

`at()` on `handle_to_body` cannot miss: every joint endpoint is by construction also in `link_objects`.

### D7: `SceneBuilder::RegisterExistingBody(ObjectHandle)`

A public method that appends the handle to `m_bodies` and returns the new `BodyId`. This reuses `BuildPhysicsReadback`'s existing `FindRigidBodyByObjectHandle` path — URDF bodies become first-class bodies with zero changes to readback code. Unlike `AddBox` etc., it performs no scene construction (the GameObject already exists with its `RigidBodyComponent`).

### D8: Error handling split

File-level failures (missing file, XML parse error, no links) throw `std::runtime_error` from `LoadUrdf`; structural quirks (joint referencing a missing link, missing `<inertial>`, prismatic/floating) keep the existing silent-skip/log-warn behavior of the builder. `ParseUrdf` itself stays log-only so the legacy save path is untouched.

### D9: Test scene organization

Extend `test/physics_app_windowed_test.cpp`: add `AddUrdfRobotScene(app)` that calls `LoadUrdf` with `a1.urdf`, `position ≈ (6, 0, 1.5)` (a clear spot inside the `AddTemplateScene2` arena, off the central random-object pile), identity rotation, default friction; then verify the returned maps (e.g. `trunk` present, `floating_base` absent) and let the robot free-fall onto the existing ground. `AddTemplateScene2` keeps running; `AddTemplateScene` stays commented out. This scene becomes the primary windowed test content going forward (user decision).

## Risks / Trade-offs

- [UrdfLoader.cpp grows a large public method] → The extraction is a move, not new logic; the diff is dominated by cut/paste plus map collection and option application.
- [Two struct families (Framework `UrdfBuiltRobot` vs AppPhysics `UrdfImportResult`) look redundant] → Deliberate: `PhysicsApp.h` must not expose Framework URDF types in its public header, keeping the library surface clean; `PhysicsApp.cpp` translates between them.
- [STL containers in `FRAMEWORK_API` structs cross the DLL boundary] → Already the established pattern in `UrdfTypes.h` (`UrdfRobot` with `vector<string>` etc.); same toolchain across DLLs, accepted risk.
- [`BuildAndSaveSceneAsset` behavior drift after rewiring] → The save path uses the identical builder with default options and null root parent, so the produced prefab is byte-compatible in structure; existing scenarios in `urdf-import` still hold.
- [A1 free-falls and may tumble in the test] → Expected physics outcome (no root anchoring by design); the test asserts free-fall, not a stable pose.
- [Hash-based material colors differ from the color-string API] → Accepted: visual assembly stays inside the Framework builder (user decision); the color-API stays `SceneBuilder`-only.

## Migration Plan

No migration: `LoadUrdfResource` keeps its signature and output. The internal rewire is invisible to callers. The new APIs are additive.
