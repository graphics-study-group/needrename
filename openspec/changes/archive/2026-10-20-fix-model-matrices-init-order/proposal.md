## Why

`PhysicsApp` starts paused, and a paused frame renders physics-driven objects from the render system's model matrices buffer as its only transform source. `CommitScene` seeds that buffer once from the initial poses (`app/physics/PhysicsApp.cpp:753-779`), but the seed is not the last writer: `SceneDataManager::Create` had already enqueued a 128-entry identity fill into the frame manager's deferred upload batch (`engine/Render/RenderSystem/SceneDataManager.cpp:169-175`), and that batch is submitted at the first frame's `SubmitFrame` (`engine/Render/RenderSystem/FrameManager.cpp:291`) — after the seed. On the graphics queue the identity copy therefore executes *after* the seed and wins. Nothing writes the buffer again until the first `Step`, so every paused frame renders each physics body with an identity model matrix — all bodies collapsed at the origin — until the user resumes.

The seed is correct and the creation-time fill is correct; only their order is unspecified. The fill is an initialization prerequisite that is published through a deferred path, so it competes with any producer that writes the buffer before the first frame is submitted. `stable-buffer-identity` introduced both halves and already recorded the physics app's commit-time seed as one of "three accidental guarantees"; this change removes the half the engine itself creates.

## What Changes

- **The identity initialization completes before `SceneDataManager::Create` returns.** The fill is submitted and waited for inside `Create` instead of being left in the frame manager's deferred upload batch for a later frame to submit. It remains a single one-time upload of `MAX_MODEL_MATRICES` identity matrices; it is not a model-matrix production.
- **The contract is stated where hosts read it.** `SceneDataManager::Create`'s documentation records that the buffer holds identity matrices and that the initialization has completed on the device when it returns.
- **`PhysicsApp` is unchanged.** `CommitScene`'s commit-time seed remains the only producer before the first step, so the app keeps exactly one production per transition: one at commit, one per `Step`, and none per paused frame. No per-frame production is added and `Step`'s production is not removed.
- **The seed's ordering assumption becomes explicit.** The seed site gains a comment stating that no engine-side write to this buffer is pending after render-system creation, so this seed is the last writer until the first `Step`.
- **A headless regression test pins both halves.** It asserts that the buffer reads as identity immediately after render-system creation, and that a producer writing the buffer before any frame is submitted still sees its own content after the deferred upload batch is flushed.
- **Out of scope (recorded to avoid scope creep):** preserving buffer contents across a storage reallocation (a `std::vector`-style copy-on-grow is a future reallocation mechanism, not this change); per-frame model matrix production in `PhysicsApp`; the pause model; and the unrelated `physics-app` requirement that still claims `Pause`/`Resume` wrap `PhysicsScene::SetSimulationEnabled`, which contradicts `physics-app-pause` and has no behavioural effect.

## Capabilities

### New Capabilities

### Modified Capabilities

- `render-graph-model-matrix-input`: the `SceneDataManager owns the model matrices buffer` requirement gains an initialization-completion clause — the identity fill SHALL be complete on the device before `Create` returns and SHALL NOT be left in a deferred upload batch — with scenarios for "initialization precedes any producer" and "nothing produced still reads identity".

## Impact

**Code**

- `engine/Render/RenderSystem/SceneDataManager.cpp` — submit and wait for the identity fill inside `Create`; `SceneDataManager.h` — record the new `Create` contract.
- `app/physics/PhysicsApp.cpp` — comment at the commit-time seed only; no logic change.
- `test/engine/headless/model_matrices_initialization_test.cpp` (new) and its headless-test CMake registration.

**Behaviour**

- One additional startup-only submit and wait on the graphics queue, before any frame is submitted. Steady-state cost is zero: no frame, step or draw issues additional work.

**API**

- No signature changes. `SceneDataManager::Create` gains a guarantee: the model matrices buffer is initialized to identity and that initialization has completed when it returns.

**Dependencies**

- Relies on the existing `SubmissionHelper::ExecuteSubmissionImmediately` path and on the commit-time seed introduced by `stable-buffer-identity` / `physics-app-pause`; neither changes.

**Tests**

- New headless regression as above. The existing `model_matrices_ownership_test`, `skybox_test`, `physics_app_offscreen_test` and `physics_app_windowed_test` are unaffected; the windowed test remains the manual end-to-end check.
