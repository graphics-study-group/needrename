## 1. Kernel facility core

- [ ] 1.1 Implement device-level kernel identity and caching keyed by the SPIR-V module: a lazy request returns the same kernel for the same module and creates the pipeline, descriptor set layout and pipeline layout exactly once. Verify: a headless unit test requests the same module twice from two different code paths and observes one pipeline (counted through the facility's own observability or a debug name query), and requests two different modules and observes two.
- [ ] 1.2 Build the resolved interface table at kernel creation from the reflected set/binding decorations and `interface_name_mapping`, so no descriptor set or binding number is stated on the C++ side. Verify: a unit test reflects a shader whose interfaces sit at non-contiguous set/binding numbers and dispatches it successfully without any C++ change.
- [ ] 1.3 Implement dictionary dispatch: a single entry point taking the command buffer, name→resource entries, the three workgroup counts and the push value; entries accept a buffer (with optional offset/size) and a texture (with optional subresource range). Verify: a headless test dispatches a shader with two storage buffers bound to different sub-ranges and reads back the expected results.
- [ ] 1.4 Implement texture binding for sampled and storage image interfaces with the layout their declared type requires. Verify: a headless test dispatches a kernel that samples an input texture and writes an output storage image, and compares the output against the expected values.
- [ ] 1.5 Implement validation: throw `std::runtime_error` naming the shader and the offending interface when the dictionary names an undeclared interface or omits a declared one; remove any path that silently skips an unbound declared interface. Verify: unit tests cover both directions and assert the message contains the shader name and the interface name; a third test dispatches a complete dictionary without throwing.
- [ ] 1.6 Implement push constants: declare the pipeline-layout range from the reflected `push_constant_size` (no range when the size is 0) and record the supplied value at offset 0 with a debug assertion that it does not exceed the reflected size. Verify: unit tests cover a 16-byte block (range covers `{eCompute, 0, 16}`), a shader with no block (no range), and an oversized value asserting in a debug build.
- [ ] 1.7 Confirm and document that dispatch records no barrier and takes no slot/rotation/frame parameter. Verify: a test records two consecutive dispatches with no caller barrier and asserts the command buffer contains no barrier between them, and the dispatch signature is inspected to contain no slot-like parameter.

## 2. SPIR-V loader consolidation

- [ ] 2.1 Add one shared physics SPIR-V loader that resolves a source-relative path against `ENGINE_PHYSICS_SPIRV_DIR` and throws `std::runtime_error` naming the absolute path attempted on a missing, empty, or non-multiple-of-four file. Verify: a unit test requests a present module and a missing one, asserting the success path and the exact failure contract.
- [ ] 2.2 Point every existing caller at the shared loader and delete the eight component-local copies (`XPBDGpuSolver`, `RadixSort`, `SumByKey`, `ParallelScan`, `CompactUnique`, `SpatialHashBroadDetector`, `ConvexCollisionDetector`, `DummySolver`). Verify: a repository search finds exactly one loader definition, and any test asserting on the loader's message still passes.

## 3. Algorithm migration

- [ ] 3.1 Migrate `ParallelScan` to kernel dispatch and remove its per-instance stage and binding members. Verify: the parallel-scan fixture passes unchanged, including the case where each pass previously used its own parameter buffer.
- [ ] 3.2 Migrate `RadixSort` to kernel dispatch, including its internally rebuilt `ParallelScan`. Verify: `gpu_radix_multiblock_test` and the primary-path fixture pass unchanged, and the buffer the sort reports as holding the result is unchanged.
- [ ] 3.3 Migrate `SumByKey` to kernel dispatch. Verify: `gpu_sum_by_key_test` passes unchanged.
- [ ] 3.4 Migrate `CompactUnique` to kernel dispatch. Verify: `gpu_compact_unique_test` passes unchanged.

## 4. Detector migration

- [ ] 4.1 Migrate `SpatialHashBroadDetector`: acquire its kernels in `Configure`, dispatch by dictionary in `Record`, and delete its stage and binding members. Verify: the detector-backed physics fixtures pass unchanged, and an instrumented run shows `Record` creating no pipeline.
- [ ] 4.2 Migrate `ConvexCollisionDetector` the same way, including the shared `clear_int_buffer` module that it previously loaded independently of the solver. Verify: the detector-backed physics fixtures pass, and the shared module resolves to the same kernel from both components.

## 5. Solver migration

- [ ] 5.1 Migrate `XpbdGpuSolver` to kernel dispatch: acquire kernels in `PreGPUStep`, dispatch by dictionary in `GPUStep`, and delete its ~18 stage and binding members. Verify: the XPBD headless fixtures and the physics-app stability test pass unchanged, with the same recorded dispatch sequence and barrier placement as before.
- [ ] 5.2 Migrate `DummySolver` and delete its stage and binding members. Verify: the dummy-solver path in the physics fixtures passes, and `GPUStep` creates no pipeline.
- [ ] 5.3 Migrate the remaining compute consumers: the windowed compute-shader test (which currently exercises the named uniform-block-variable path through the deleted binding type) and any other test or example that constructs a compute stage. Verify: the affected tests pass with the variable bound as an ordinary uniform buffer through the dictionary, and no test references a deleted type.

## 6. Bloom migration and type deletion

- [ ] 6.1 Migrate the bloom pass to kernel dispatch, binding `inputImage` and `outputImage` as texture entries, and delete its stage and binding members. Verify: a windowed run renders the bloom effect with output matching the pre-change behaviour.
- [ ] 6.2 Delete `ComputeStage`, `ComputeResourceBinding` and `ComputeHelpers`, and remove `ComputeResourceBinding.*`, `ComputeStage.*` and `ComputeHelpers.h` from the module. Verify: the build succeeds and a repository search finds no reference to any of the three.
- [ ] 6.3 Delete `CommandBuffer::BindComputeStage`, `CommandBuffer::BindComputeResource`, `CommandBuffer::DispatchCompute` and `m_bound_compute_stage`. Verify: the build succeeds, and a search finds no reference to the deleted wrappers.
- [ ] 6.4 Confirm the surviving material path is untouched: `ShaderResourceBinding`, `IndexedBuffer`, `StructuredBuffer` and `StructuredBufferPlacer` still exist and `MaterialInstance` compiles and renders unchanged. Verify: a windowed run renders materials correctly, and the material-side tests pass.

## 7. Verification

- [ ] 7.1 Run the full suite in both configurations. Verify: `cmake --build --preset msvc-debug` then `ctest --preset msvc-debug` pass, and the release configuration builds.
- [ ] 7.2 Confirm the no-rotation and no-duplication outcomes stated in the specs. Verify: a search of `engine/Physics/` finds no `AllocateResourceBinding`, `slot_count`, `BindComputeResource` or `m_frame_counter`, and the compute-kernel spec's scenarios for kernel identity, validation, push constants and the no-barrier rule are all exercised by tests.
- [ ] 7.3 Hand the deferred spec sweep to `physics-step-simplification`. Verify: that change's `tasks.md` contains a task to migrate the four capabilities listed in `design.md` D12 (`gpu-parallel-scan`, `gpu-convex-collision-detection`, `spatial-hash-broad-phase`, `xpbd-solver-multi-rg`) off the deleted types before it archives.
- [ ] 7.4 Validate the change. Verify: `openspec validate compute-kernel-dispatch --strict` reports the change valid.
