# Design: Structured buffer placer block size

## Context

`StructuredBufferPlacer` stores per-variable `(offset, size)` pairs and derives a single size, `CalculateMaxSize()`, as `max(offset + size)`. That one number is consumed twice: by `WriteBuffer(data, vector)` to size the staging vector (footprint of member writes), and by `ComputeResourceBinding::PrepareIndexedBuffers` / `MaterialInstance::CreatePassInfo` to size GPU UBO slices and their descriptor ranges. SPIR-V records only member offsets, so SPIRV-Cross's `get_declared_struct_size` is the same last-member-end formula (`spirv_cross.cpp:2033`); the std140 block size is that value rounded up to the structure's base alignment. The existing `rhi-push-constants` capability deliberately uses the unrounded declared size for `push_constant_size` — push constants only need to cover accessed members, while a UBO descriptor range must cover the whole block. See proposal.md for motivation.

## Goals / Non-Goals

**Goals:**

- Give the placer two explicit sizes: `GetRequiredSize()` (write footprint) and `GetBlockSize()` (target-layout block size including trailing padding), with the invariant `required <= block`.
- Have shader reflection inject the std140-rounded block size, so GPU UBO allocations and descriptor ranges always cover the full block.
- Rewrite the unit test to be layout-deterministic on MSVC/Clang/GCC and add regression coverage for padded, aligned, nested, and reflected cases.

**Non-Goals:**

- No change to `push_constant_size` semantics (owned by `rhi-push-constants`).
- No change to member write logic, RTTI type checks, or the `WriteBuffer` member loop.
- No change to `SceneDataManager` / `CameraManager` CPU mirror structs (alignas/static_assert hardening is a separate change).
- No nested-struct support in reflection (remains asserted unsupported).
- No zeroing of staging tails (padding bytes are never read; optional hygiene only).

## Decisions

### D1: Two accessors, not one maxed value

`GetRequiredSize()` keeps the existing formula; `SetBlockSize(size_t)` + `GetBlockSize()` carry the declared block size. `WriteBuffer(data, vector)` resizes by required size; GPU allocation sites use block size.

- **Alternative**: keep one `CalculateMaxSize()` returning `max(declared, required)`. Minimal diff, but the staging vector grows to block size for no reason and the two contracts stay indistinguishable in callers.
- **Alternative**: keep the placer staging-only and store the block size on `SPInterfaceStructuredBuffer`. Splits one invariant (`required <= block`) across two classes and two call paths; more drift surface.

### D2: The block size is injected by the caller, never derived

`(offset, size)` pairs contain no alignment information, so trailing padding is not derivable inside the placer. The placer must also not hardcode GPU rules because it equally mirrors CPU structs (the unit test). The knowledge lives with the caller: the test knows `sizeof(T)`; `ReflectSimpleStruct` knows SPIR-V plus the std140 rules.

- **Alternative**: pass `alignof(T)` per variable and round up inside the placer. The placer would then need two rule sets (C++ `alignof` for the CPU path vs std140 array/struct rounding for the GPU path); rejected as a responsibility split.

### D3: Reflection rounding rule

After the member loop, `ReflectSimpleStruct` computes `declared = compiler.get_declared_struct_size(type)` and injects `SetBlockSize(align_up(declared, struct_align))` where `struct_align = 16` when any vec4/matrix member was seen, else `4`. This matches std140's structure base alignment for the member set the function admits (scalars, vec4, mat4 — everything else asserts today).

- **Alternative**: always round to 16. Simpler, but mislabels scalar-only blocks (e.g. `fluid.comp`'s 4-byte UBO becomes 16) and wastes bytes; `IndexedBuffer` already rounds slices up to `minUniformBufferOffsetAlignment` where required.

### D4: Fallback and assertion semantics

`GetBlockSize()` returns the injected value, or `GetRequiredSize()` when unset (preserving the historical packed-layout behavior for callers that never declare a block size). The debug assertion `declared >= required` lives in `GetBlockSize()` (recomputing required), not in `SetBlockSize`, so it also catches the mistake of adding members after setting the block size.

### D5: Nested recursion uses the child's required size

The parent's required size must cover the child's *write* extent, so recursion contributes `offset + child.GetRequiredSize()`. The child's block size does not enter the parent's required computation; the parent's block size is caller-declared (`sizeof(parent)`). With the test layouts this yields sub: 28/32, super: `max(8, 8+28, 40+64) = 104` / block 104.

### D6: Test drops `[[gnu::packed]]` instead of making packing portable

Without packing, `sub_buffer` (`uint32_t`/`double`/`float[3]`) lays out identically on x64 MSVC, Clang, and GCC: members at 0/8/16, `sizeof == 32`. The test then exercises exactly the trailing-padding case that the old code got wrong, on every platform. Exact-number assertions pin both sizes; the round-trip memcpy uses a buffer sized exactly `GetBlockSize()` (replacing the 1024-byte buffer) so any write past the block is caught (ASan) rather than hidden.

- **Alternative**: `#pragma pack(push,1)` portable packing. Keeps the packed model, but packs `double` to offset 4 (unaligned on strict platforms) and leaves the padding case untested.

### D7: Reflection regression test reuses the glslang test path

`structured_buffer_test` links `Engine` by default (`anro_add_test`), so it can call `SPLayout::Reflect`. The test compiles small GLSL blocks via the existing `ShaderCompiler` (glslang, as in `test/unit/shader/shader_compile_test`) and reflects them with `filter_out_low_descriptors = false`; it then finds each UBO interface by name, `dynamic_cast`s to `SPInterfaceStructuredBuffer`, and asserts the placer's required/block sizes: `{ vec4; float; }` → 20/32, `{ uint; }` → 4/4, `{ vec4; mat4; }` → 80/80.

### D8: Call-site changes are allocation-only

`ComputeResourceBinding.cpp:65` and `MaterialInstance.cpp:87` swap `CalculateMaxSize()` for `GetBlockSize()`. Everything downstream is already correct: the descriptor range is `IndexedBuffer::GetSliceSize()` (now the block size), and the per-frame `memcpy` copies `cpu_side_buffer.size()` (staging = required size) into the slice — the slice's tail stays stale padding, which is never read.

## Risks / Trade-offs

- [Public API rename breaks external users] → `StructuredBufferPlacer` is an internal engine DLL (`RHI_API`) with three call sites plus the test, all updated in the same change.
- [Caller injects a non-std140 size for a UBO] → doc comment on `SetBlockSize` states the contract ("includes trailing padding; `sizeof(T)` for CPU mirrors, std140-rounded size for reflected UBOs"); the debug assert catches only undersized values, and oversized values are legal.
- [Rounding rule becomes incomplete when reflection grows new member types (vec2/vec3/arrays/structs)] → `ReflectSimpleStruct` asserts on those types today; whoever adds them must extend the struct-alignment rule, and the reflection regression test makes the expected sizes explicit.
- [Tail bytes of UBO slices stay uninitialized] → padding is never read by shaders; zeroing is optional hygiene and explicitly out of scope.
- [std140 vs std430 confusion] → the rounding rule is applied only in the UBO path of `ReflectSimpleStruct`; SSBOs get no placer today, and push constants keep their existing declared-size semantics under `rhi-push-constants`.

## Migration Plan

Single-step, no data or format migration: rename the accessor, add the injection API, update the two allocation sites and the test in the same change. Rollback is a revert; allocation sizes for all current shaders are byte-identical before and after (every existing reflected block ends on a 16-byte boundary), so no runtime state is affected.

## Open Questions

None.
