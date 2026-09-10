# gpu-sum-by-key

## Purpose

Defines the contract for a reusable GPU segmented reduction algorithm (`SumByKey` class and its compute shader) that reduces sorted `(key, value)` arrays into per-key sums through a recursive block-level reduction using shared-memory doubling, with N float value channels batched into a single buffer.

## ADDED Requirements

### Requirement: SumByKey class construction

The `SumByKey` class SHALL be constructible with `(Rhi::DeviceContext &device_context, uint32_t max_entries, uint32_t max_key_value, uint32_t num_channels)`. The constructor SHALL NOT allocate any GPU resources; shader loading and `ComputeStage` instantiation SHALL be deferred until the first `Record` call.

The class SHALL reside in `engine/Physics/gpu_algorithm/` and SHALL NOT depend on any detector, solver, or collision-specific types. Dependencies SHALL be limited to the Rhi compute infrastructure (`ComputeBuffer`, `ComputeStage`, `ComputeResourceBinding`, `DeviceContext`) plus `vk::CommandBuffer`.

`num_channels` SHALL be in `[1, 8]`; `max_entries` SHALL be greater than zero. Violations SHALL throw `std::invalid_argument`.

#### Scenario: Construction with valid parameters

- **WHEN** `SumByKey` is constructed with `max_entries = 200000`, `max_key_value = 4096`, `num_channels = 7`
- **THEN** no GPU resources are allocated
- **AND** no shaders are loaded
- **AND** no exceptions are thrown

#### Scenario: Construction rejects invalid channel count

- **WHEN** `SumByKey` is constructed with `num_channels = 9`
- **THEN** a `std::invalid_argument` exception is thrown

### Requirement: Static sizing helpers

The `SumByKey` class SHALL expose static sizing helpers so callers can allocate all working buffers before recording:

- `GetNumLevels(uint32_t max_entries)` SHALL return the recursion depth `k`: `R_0 = max_entries`, `R_{i+1} = 2 * ceil(R_i / 256)` until `R_k <= 256`. `k` SHALL be at least 1 and at most 4 for `max_entries <= 2^28`.
- `GetRequiredRecordsBytes(uint32_t max_entries, uint32_t num_channels)` SHALL return `(R_1 + R_2 + ... + R_{k-1}) * (4 + 4 * num_channels)` bytes — the total size of the record regions (one key uint plus `num_channels` floats per record). When `k == 1` the result SHALL be 0.
- The level offsets of each record region SHALL be fixed at construction time and SHALL NOT change between `Record` calls.

#### Scenario: Record sizing for the contact array

- **WHEN** `GetRequiredRecordsBytes(200000, 7)` is called
- **THEN** the level sequence is `R_1 = 1564`, `R_2 = 14`, and the result is `1578 * 32 = 50496` bytes

#### Scenario: Single-level reduction needs no record buffer

- **WHEN** `GetRequiredRecordsBytes(256, 7)` is called
- **THEN** the result is 0 bytes
- **AND** `GetNumLevels(256)` returns 1

### Requirement: Record dispatches the recursive level chain

`Record` SHALL accept the sorted key array, the packed value buffer, the record buffers, and the output buffer, and SHALL dispatch exactly `k` compute passes (one per level) with a full compute barrier between consecutive levels:

- Level 0 SHALL read `max_entries` entries from the caller's sorted keys and values and SHALL write boundary records into record region 1.
- Levels `1 .. k-2` SHALL read record region `i` and SHALL write record region `i+1`.
- Level `k-1` (a single workgroup covering `R_{k-1} <= 256` records) SHALL write all remaining per-key sums directly to the output buffer.

All per-level parameters (region offsets, level element count, workgroup count, channel stride, channel count, key bound) SHALL travel via the shader's push-constant block. The caller SHALL insert the outer barriers around the whole `Record` (before the first level and after the last), matching the `RadixSort`/`ParallelScan` conventions.

#### Scenario: Full recursion for a large array

- **WHEN** `Record` is called for a 200000-entry reduction (`k = 3`)
- **THEN** three compute dispatches are recorded (782, 7, and 1 workgroups respectively)
- **AND** a barrier is recorded between consecutive dispatches

#### Scenario: Single level for a small array

- **WHEN** `Record` is called for a reduction with `max_entries <= 256`
- **THEN** exactly one compute dispatch is recorded
- **AND** no record-buffer bindings are written by the shader

### Requirement: Recursive segmented reduction correctness

The reduction SHALL produce, for every key `b < max_key_value` present in the sorted input, `out[b] = sum of the values of all entries with key b`, in exact per-channel order. Entries whose key is greater than or equal to `max_key_value` (including the `0xFFFFF` INVALID sentinel) SHALL be ignored and SHALL NOT write to the output. Keys absent from the input SHALL leave their output slots untouched.

The algorithm SHALL require the input keys to be sorted in ascending order. Relative order of entries within the same key SHALL not affect the per-key result set (sums of the same value set; floating-point rounding may vary with order).

Each block SHALL process 256 entries: load into shared memory, perform same-key pairwise doubling with dead marking, write per-key sums for segments fully contained in the block directly to the output, and emit at most two boundary records per block (first segment partial, last segment partial; the second record SHALL be an all-zero record when the whole block belongs to one segment).

#### Scenario: Single key spanning many blocks

- **WHEN** the sorted input contains 10000 entries all with key 5 and values all 1.0
- **THEN** `out[5]` equals 10000.0 after `Record` completes

#### Scenario: Many keys within one block

- **WHEN** the sorted input contains 100 entries with keys 0..99 each appearing once with value 2.0
- **THEN** `out[b]` equals 2.0 for every `b` in 0..99

#### Scenario: INVALID sentinel entries are ignored

- **WHEN** the sorted input contains valid entries plus a trailing run of entries with key `0xFFFFF` (greater than `max_key_value`)
- **THEN** no output slot outside `[0, max_key_value)` is written
- **AND** all valid per-key sums are unaffected

#### Scenario: Absent keys keep their output untouched

- **WHEN** the sorted input contains no entry with key 3
- **THEN** `out[3]` retains whatever value it held before `Record`

### Requirement: N-channel batched values

The value buffer SHALL store `num_channels` float channels in channel-major layout: the value of channel `c` at element `i` SHALL reside at `values[c * stride + i]`, where `stride` is the current level's element count. The output buffer SHALL use the same channel-major layout with stride `max_key_value`.

The channel count SHALL be passed to the shader via the push-constant block at record time; a single `Record` call SHALL reduce all channels with one pass per level (no per-channel dispatches).

#### Scenario: Seven channels reduce in one pass per level

- **WHEN** `Record` is called with `num_channels = 7` over sorted entries
- **THEN** each of the 7 per-key sums is written to its channel region of the output buffer
- **AND** the number of dispatches equals the level count, independent of `num_channels`

### Requirement: Shader file location

The `SumByKey` compute shader SHALL reside at `engine/Physics/shader/algorithm/sum_by_key.comp` and SHALL compile to SPIR-V at `build/engine/Physics/spirv/algorithm/sum_by_key.comp.spv` via the physics shader build pipeline.

#### Scenario: Shader compiles to SPIR-V

- **WHEN** CMake is configured for the physics target
- **THEN** `sum_by_key.comp` compiles to `sum_by_key.comp.spv` in the spirv output directory
