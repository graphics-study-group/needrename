# Proposal: Structured buffer placer block size

## Why

`StructuredBufferPlacer::CalculateMaxSize()` computes `max(offset + size)` — the end of the last member — and the same number serves two different jobs: the staging-buffer footprint for `WriteBuffer`, and the GPU UBO allocation/descriptor size in `ComputeResourceBinding` and `MaterialInstance`. The formula omits trailing padding, so it is only valid for packed layouts: on MSVC the unit test fails (`CalculateMaxSize() == 28` vs `sizeof(sub_buffer) == 32`, because MSVC ignores `[[gnu::packed]]`), and any future std140 UBO whose last member ends short of a 16-byte boundary would under-allocate GPU buffers and violate descriptor-range requirements (`VUID-vkCmdDraw-None-02686`).

## What Changes

- **Split the placer's size semantics** (**BREAKING**):
  - `GetRequiredSize()` — the staging footprint, exactly the current `max(offset + size)` formula (recursion uses the child's required size). Used by `WriteBuffer(data, vector)` for the resize.
  - `SetBlockSize(size_t)` / `GetBlockSize()` — the target-layout block size **including trailing padding**, injected by the caller who knows the layout. `GetBlockSize()` falls back to `GetRequiredSize()` when unset and asserts (debug) that the injected value covers the required size.
  - `CalculateMaxSize()` is removed; all call sites move to one of the two accessors.
- **Reflection injects the std140 block size**: `ReflectSimpleStruct` computes `get_declared_struct_size` rounded up to the struct's base alignment (16 when any vec4/mat4 member exists, else 4) and calls `SetBlockSize`.
- **GPU allocation sites use `GetBlockSize()`**: `ComputeResourceBinding::PrepareIndexedBuffers` and `MaterialInstance::CreatePassInfo` allocate UBO slices by block size, so descriptor ranges cover the full std140 block.
- **Unit test rewritten to be cross-platform**: drop `[[gnu::packed]]`, inject `SetBlockSize(sizeof(...))`, and assert exact sizes (padded case: required 28 / block 32; nested case: required 104 / block 104) plus the `required <= block` invariant and byte round-trips against a buffer sized exactly by block size.
- **New regression coverage**: a padded CPU struct (A < B), an aligned CPU struct (A == B), the nested sub/super pair, and a reflection-path case that compiles a trailing-padding std140 block via glslang and asserts the reflected block size (e.g. `{ vec4; float; }` → 32) and required size (20).

## Capabilities

### New Capabilities
- `rhi-structured-buffer-placer`: `Rhi`'s `StructuredBufferPlacer` exposes two explicit size semantics — a computed required size (staging write footprint) and a caller-declared block size (target-layout size including trailing padding); shader reflection injects the std140-rounded block size, GPU UBO bindings allocate by block size, and tests pin exact sizes for padded, aligned, nested, and reflected blocks.

### Modified Capabilities
- none

## Impact

- **API**: `engine/Rhi/Buffer/StructuredBufferPlacer.h/.cpp` — `CalculateMaxSize()` replaced by `GetRequiredSize()` / `SetBlockSize()` / `GetBlockSize()`.
- **Reflection**: `engine/Rhi/Pipeline/ShaderParameterLayout.cpp` (`ReflectSimpleStruct`).
- **Call sites**: `engine/Rhi/Pipeline/ComputeResourceBinding.cpp`, `engine/Render/Pipeline/Material/MaterialInstance.cpp` (allocation only; member writes, staging resize, and descriptor binding logic are unchanged).
- **Tests**: `test/unit/core/structured_buffer_test.cpp`; reflection regression uses the existing glslang test infrastructure (`ShaderCompiler`, as in `test/unit/shader/shader_compile_test`).
- **Behavior**: no change for current shaders — every existing reflected block already ends on a 16-byte boundary, so allocation sizes are byte-identical; the fix only changes behavior for blocks with trailing padding. The push-constant path (`push_constant_size`, `get_declared_struct_size` without tail rounding) is intentionally unchanged, per the existing `rhi-push-constants` capability.
