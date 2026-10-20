## 1. Engine fix

- [x] 1.1 In `SceneDataManager::Create` (`engine/Render/RenderSystem/SceneDataManager.cpp:169-175`), call `ExecuteSubmissionImmediately()` on the frame manager's `SubmissionHelper` immediately after the identity fill is enqueued and before the default light-map clear, with a comment naming the ordering rule (initialization completes before `Create` returns). Verify `cmake --build --preset msvc-debug --target EngineRender` succeeds.
- [x] 1.2 Record the completion clause in `SceneDataManager::Create()`'s documentation in `engine/Render/RenderSystem/SceneDataManager.h`: the model matrices buffer holds identity matrices and the initialization has completed on the device when it returns. Verify by reading the header diff.
- [x] 1.3 Add the ordering comment at the commit-time seed in `app/physics/PhysicsApp.cpp:753-779` — the seed is the last writer until the first `Step` because no engine-side write to the buffer is pending after render-system creation. No logic change; verify `cmake --build --preset msvc-debug --target PhysicsApp` succeeds.

## 2. Regression test

- [x] 2.1 Create `test/engine/headless/model_matrices_initialization_test.cpp`, with a header comment recording the two-writer timeline and the `ExecuteSubmissionImmediately` / `ExecuteSubmission` equivalence. Initialize `MainClass` headless, copy the model matrices buffer into a staging buffer and assert it holds identity without any frame having been submitted (scenario "Identity is readable before any frame is submitted").
- [x] 2.2 Extend that test with the producer scenario: write a known pose through `PhysicsSystem::GPUCalcModelMatrices`, flush the frame manager's staged uploads, read the buffer back and assert the producer's values survive (scenario "Initialization precedes any producer"). Verify the assertion fails with 1.1 reverted and passes with it restored.
- [x] 2.3 Register the test with `anro_add_test(model_matrices_initialization_test FOLDER tests/engine/headless)` in `test/engine/headless/CMakeLists.txt` and verify `ctest --preset msvc-debug -R model_matrices_initialization` passes.

## 3. Integration verification

- [x] 3.1 Run `ctest --preset msvc-debug` and confirm `model_matrices_ownership_test`, `skybox_test`, `physics_app_offscreen_test`, `physics_app_windowed_test` and `physics_app_physics_only_test` still pass.
- [x] 3.2 Manual windowed check from a clean start: the first paused frame shows the scene's initial layout (bodies at their placed poses) instead of everything collapsed at the origin; then SPACE (resume) and SPACE again (pause) and confirm the paused view stays correct.
- [x] 3.3 `openspec validate fix-model-matrices-init-order --strict` passes.
