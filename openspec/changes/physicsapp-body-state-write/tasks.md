# Tasks: PhysicsApp per-body state writes

## 1. API surface

- [x] 1.1 Add `enum class BodyField` (Position, Rotation, LinearVelocity, AngularVelocity, ExternalForce, ExternalTorque) to `PhysicsApp.h` with per-enumerator doc comments
- [x] 1.2 Add `void SetBodyValue(BodyId id, BodyField field, glm::vec4 value)` declaration to `PhysicsApp` (Drive-phase section) with doc contract: value layout (quat xyzw for Rotation, xyz + w=0 otherwise), COM world space, direct-overwrite persistence, caller-managed force lifetime, call-only-between-Steps contract
- [x] 1.3 Implement `SetBodyValue` in `PhysicsApp.cpp`: phase check (`std::logic_error`), BodyId validity check (`std::out_of_range`), `body_to_slot` mapping, staging write, `any_dirty[field] = true`
- [x] 1.4 Normalize the quaternion on write for `BodyField::Rotation`

## 2. Upload staging and Step integration

- [x] 2.1 Add Impl members: six persistent `StagingToDevice` buffers (one per field), `std::array<bool, 6> any_dirty`
- [x] 2.2 Allocate the six staging buffers in `CommitScene` next to `BuildPhysicsReadback` (size `slot_count * sizeof(glm::vec4)`, capture the six source GPU buffers from `GetGpuBuffers()`)
- [x] 2.3 Add `RecordBodyStateUpload(vk::CommandBuffer)` helper: for each dirty field record one whole-buffer `copyBuffer(staging → GPU buffer)`
- [x] 2.4 Record `RecordBodyStateUpload` in `Step`'s command buffer BEFORE `GPUStep` (keep readback copy after it); clear `any_dirty` after the fence wait

## 3. Pause model rework

- [x] 3.1 Remove `SetSimulationEnabled` calls from `Pause()` / `Resume()` (flag-only) and from `CommitScene`'s post-freeze block
- [x] 3.2 In `CommitScene`, call `SetSimulationEnabled(true)` exactly once; keep `paused = true` initial flag
- [x] 3.3 Make `Step()` unconditional: remove any paused-state gating and update its doc comment (drop "no-op while paused")
- [x] 3.4 Keep the SPACE toggle in `RenderNextFrame` unchanged (it flips the app flag); verify `IsPaused()` doc matches the new model

## 4. Tests

- [x] 4.1 Update `test/physics_app_windowed_test.cpp` main loop: call `Step()` only when `!IsPaused()`, keep `RenderNextFrame()` unconditional
- [x] 4.2 Add `SetBodyValue` scenarios to `test/physics_app_physics_only_test.cpp`: pre-commit throw, invalid BodyId throw, position teleport applies on next Step, position does not snap back on later steps, velocity injection evolves, force persists across steps until zeroed, unset fields not re-uploaded
- [x] 4.3 Add pause-model scenario: `Step()` advances physics while the paused flag is set; `IsSimulationEnabled()` stays true across Pause/Resume
- [x] 4.4 Build and run `ctest --preset debug` (all physics app tests) with the MSYS2 CLANG64 environment per `docs/build_instructions/windows_msys2_clang64.md`
