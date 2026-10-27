## 1. Placer API split

- [x] 1.1 In `engine/Rhi/Buffer/StructuredBufferPlacer.h/.cpp`, rename `CalculateMaxSize()` to `GetRequiredSize()` (same `max(offset + size)` formula), make the nested-buffer recursion use the child's `GetRequiredSize()`, and update `WriteBuffer(data, vector)` to resize by `GetRequiredSize()`; verify `cmake --build --preset msvc-debug` succeeds
- [x] 1.2 Add `SetBlockSize(size_t)` and `GetBlockSize()` to `StructuredBufferPlacer`: `GetBlockSize()` returns the injected value, falls back to `GetRequiredSize()` when unset, and asserts (debug) that the injected value covers the freshly computed required size; verify the build succeeds and the fallback path is exercised by the group 3 test before any `SetBlockSize` call is added to it
- [x] 1.3 Update the Doxygen comments for the new API (contract of `SetBlockSize`: includes trailing padding; `sizeof(T)` for CPU mirror structs, std140-rounded size for reflected UBOs; members added first) and run `.clang-format` on the changed files; verify no style-checker complaint

## 2. Reflection and call sites

- [x] 2.1 In `engine/Rhi/Pipeline/ShaderParameterLayout.cpp` (`ReflectSimpleStruct`), track whether any vec4/matrix member was seen, compute `block_size = align_up(compiler.get_declared_struct_size(type), 16 or 4)`, and call `placer->SetBlockSize(block_size)` after the member loop; verify `cmake --build --preset msvc-debug` succeeds
- [x] 2.2 In `engine/Rhi/Pipeline/ComputeResourceBinding.cpp` (PrepareIndexedBuffers), replace `placer->CalculateMaxSize()` with the block-size accessor so UBO slices and descriptor ranges cover the full std140 block; verify the build succeeds
- [x] 2.3 In `engine/Render/Pipeline/Material/MaterialInstance.cpp` (CreatePassInfo), replace the `CalculateMaxSize()` allocation with the block-size accessor; verify the build succeeds

## 3. Unit test rewrite

- [x] 3.1 In `test/unit/core/structured_buffer_test.cpp`, remove `[[gnu::packed]]`, call `SetBlockSize(sizeof(sub_buffer))` / `SetBlockSize(sizeof(super_buffer))` after registering members, and replace the `>= sizeof` assertions with exact-size assertions: sub required 28 / block 32, super required 104 / block 104, plus `GetRequiredSize() <= GetBlockSize()` for both; verify the test passes on MSVC
- [x] 3.2 Change the round-trip writes to use a buffer sized exactly `GetBlockSize()` (replace the 1024-byte buffer) and keep the member-value checks, so any write past the block size is an out-of-bounds error caught by ASan; verify the test passes
- [x] 3.3 Add an aligned struct case (e.g. two `double` members, no padding) asserting `GetRequiredSize() == GetBlockSize() == sizeof`; verify the test passes
- [x] 3.4 Build and run `structured_buffer_test` on both toolchains (MSVC `msvc-debug` preset and Linux/Clang `debug` preset) and verify it passes on both (MSVC verified locally; Linux/Clang leg to be confirmed in CI — not runnable on this Windows host)

## 4. Reflection regression test

- [x] 4.1 Add a reflection case to the structured buffer test that compiles three GLSL blocks via `ShaderCompiler` and reflects them with `SPLayout::Reflect` (filter disabled): `{ vec4 a; float b; }` → required 20 / block 32, `{ uint count; }` → 4 / 4, `{ vec4 a; mat4 m; }` → 80 / 80, asserting the `SPInterfaceStructuredBuffer` placer's sizes; verify the test passes
- [x] 4.2 Run the existing headless compute/windowed test subset to confirm no allocation-size regressions for current shaders; verify `ctest --preset msvc-debug` (or the debug preset on Linux) reports no failures

## 5. Validation

- [x] 5.1 Run the full ctest suite on the MSVC and Clang/Linux presets and verify all tests pass (MSVC `headless` suite: 19/19 passed; Linux/Clang full suite to be confirmed in CI — not runnable on this Windows host)
- [x] 5.2 Run `openspec validate fix-structured-buffer-placer-block-size` and verify the change validates cleanly
