# gpu-sum-by-key

## MODIFIED Requirements

### Requirement: SumByKey class construction

The `SumByKey` class SHALL be constructible with `(Rhi::DeviceContext &device_context)` alone. Element geometry SHALL NOT be a construction-time parameter and the instance SHALL hold no geometry state. The constructor SHALL NOT allocate any GPU resources; shader loading and `ComputeStage` instantiation SHALL be deferred until the first `Record` call.

The class SHALL reside in `engine/Physics/gpu_algorithm/` and SHALL NOT depend on any detector, solver, or collision-specific types. Dependencies SHALL be limited to the Rhi compute infrastructure (`ComputeBuffer`, `ComputeStage`, `ComputeResourceBinding`, `DeviceContext`) plus `vk::CommandBuffer`.

Because geometry is supplied per call, one instance SHALL be reusable for any geometry, including several different geometries within a single frame, and SHALL NOT require rebuilding when the caller's geometry changes.

#### Scenario: Construction allocates nothing and stores no geometry

- **WHEN** `SumByKey` is constructed with a device context
- **THEN** no GPU resources are allocated
- **AND** no shaders are loaded
- **AND** no geometry is stored on the instance

#### Scenario: One instance serves several geometries in one frame

- **WHEN** a single instance is called for reductions with different capacities, key bounds and entry counts
- **THEN** each call uses the geometry passed to that call
- **AND** no call is affected by a previous call's geometry

### Requirement: Static sizing helpers

The `SumByKey` class SHALL expose static sizing helpers so callers can allocate all working buffers before recording:

- `GetNumLevels(uint32_t capacity)` SHALL return the recursion depth `k` for that capacity: `R_0 = capacity`, `R_{i+1} = 2 * ceil(R_i / 256)` until `R_k <= 256`. `k` SHALL be at least 1 and at most 4 for `capacity <= 2^28`.
- `GetRequiredRecordsBytes(uint32_t capacity, uint32_t num_channels)` SHALL return `(R_1 + R_2 + ... + R_{k-1}) * (4 + 4 * num_channels)` bytes — the total size of the record regions (one key uint plus `num_channels` floats per record). When `k == 1` the result SHALL be 0.
- Both SHALL be pure functions of their arguments and SHALL be callable without any instance.
- The record-region offsets used by a call SHALL be derived from that call's capacity and `num_channels`, so a caller that varies its capacity SHALL size its record buffer with the largest capacity it ever passes.

#### Scenario: Record sizing for the contact array

- **WHEN** `GetRequiredRecordsBytes(200000, 7)` is called
- **THEN** the level sequence is `R_1 = 1564`, `R_2 = 14`, and the result is `1578 * 32 = 50496` bytes

#### Scenario: Single-level reduction needs no record buffer

- **WHEN** `GetRequiredRecordsBytes(256, 7)` is called
- **THEN** the result is 0 bytes
- **AND** `GetNumLevels(256)` returns 1

#### Scenario: Sizing does not require an instance

- **WHEN** the helpers are called before any `SumByKey` instance exists
- **THEN** they return the same values they would return for a constructed instance

### Requirement: Record dispatches the recursive level chain

`Record` SHALL accept the sorted `(key, slot)` pair array, the packed value buffer, the record buffer, the output buffer, and an entry-count buffer, and SHALL dispatch exactly `k` compute passes (one per level) with a full compute barrier between consecutive levels:

- Level 0 SHALL read the caller's sorted pair array, take each entry's key from `pair.x`, gather each of its `num_channels` values from `values[c * capacity + pair.y]`, and SHALL write boundary records into record region 1.
- Levels `1 .. k-2` SHALL read record region `i` and SHALL write record region `i+1`. At these levels the key of record `e` SHALL be `keys_in[e]` and channel `c` SHALL be `values_in[c * R_i + e]`.
- Level `k-1` (a single workgroup covering `R_{k-1} <= 256` records) SHALL write all remaining per-key sums directly to the output buffer.

**Two different lengths bound a level, and they SHALL NOT be conflated.**

The **level element count** is the element count of the array a level reads: the call's `capacity` at level 0 (`R_0 = capacity`), and `R_i` at level `i >= 1`. It alone SHALL determine:

- the element-index bound (`e < level element count`), which decides whether an element exists at all;
- the per-block real-extent used for run classification, and therefore which boundary records a block emits;
- the per-level workgroup count — level 0 SHALL always dispatch `ceil(capacity / 256)` workgroups so that every record slot of region 1 is rewritten on every call;
- the record-region partition (`R_{i+1} = 2 * ceil(R_i / 256)` records, one pair of slots per block);
- the level-0 channel stride of the value buffer.

The **entry count** is the number of leading elements that carry real input data. It SHALL bound **data extent only**: at level 0 no value SHALL be read for an element at or beyond it, and it SHALL have no effect on the element-index bound, on run classification, on the workgroup count, or on the record-region layout. It SHALL be read from the caller's entry-count buffer inside the shader at execution time, under the same guard-manner as the radix sort's pair count, because the producing pass runs on the GPU and the host does not know the value at record time. A call whose entry count equals the call's capacity SHALL behave exactly as if the bound were absent.

Because the level element count and not the entry count sizes level 0, every block of level 0 always writes its portion of region 1, and the record chain stays dense: each of the `2 * ceil(capacity / 256)` slots of region 1 is written on every call. Sizing level 0's workgroup count by the entry count would leave record slots from an earlier call in place, which the next level reads as phantom partials.

Elements at or beyond the entry count SHALL NOT have their **values** read. This includes the caller's pair array: the values of an entry whose index is at or beyond the entry count SHALL NOT be gathered from the value buffer, and neither SHALL the values of an entry whose payload slot is at or beyond it. Such an entry SHALL still contribute its key to the level's run structure and SHALL contribute 0.0 to every channel. Callers therefore only need valid values below the entry count, and only need the pair array itself to be writable up to its bound (the entry passes' full-capacity write contract covers that).

An entry whose payload slot is outside `[0, capacity)` SHALL contribute 0.0 to every channel and SHALL NOT be read from the value buffer, so a caller that emits a bad slot id loses that entry instead of triggering a wild read. Its key SHALL be used unchanged: rewriting the key would violate the ascending-order requirement below and could split a real key's run, losing one of its partial sums.

`num_channels` SHALL be in `[1, 8]` and `capacity` SHALL be greater than zero; a call violating either, or a `max_key_value` of zero, SHALL throw `std::invalid_argument`. `Record` SHALL additionally reject a capacity whose implied buffer sizes exceed the buffers bound for that call.

The input mode SHALL travel in the push-constant block as a `gather_pairs` flag that `Record` sets for level 0 and clears for every deeper level. In gather mode the level's input SHALL always be the `(key, slot)` pair array, and the entry-count bound SHALL apply to it. In non-gather mode the level's input SHALL always be the `KeysIn` / `ValuesIn` key and value arrays, and no entry-count bound SHALL apply, because a deeper-level input is produced by the previous level and is entirely valid. The entry-count buffer SHALL be bound at every level (its contents are read only in gather mode), so no level needs a placeholder for it.

All other per-level parameters (region offsets, level element count, workgroup count, channel count, key bound) SHALL also travel via the shader's push-constant block. The push-constant block SHALL NOT carry the entry count, which is GPU-produced. The caller SHALL insert the outer barriers around the whole `Record`.

The shader's storage-buffer bindings SHALL be `PairsIn`, `KeysIn`, `ValuesIn`, `RecKeys`, `RecValues`, `OutValues` and the entry-count buffer — seven buffers, with one pass per level for all channels.

#### Scenario: Full recursion for a large array

- **WHEN** `Record` is called with `capacity = 200000` (`k = 3`) and an entry count equal to the capacity
- **THEN** three compute dispatches are recorded (782, 7, and 1 workgroups respectively)
- **AND** a barrier is recorded between consecutive dispatches
- **AND** only the first dispatch reads `PairsIn` (`gather_pairs = 1`)

#### Scenario: Level 0 dispatch is sized by the capacity, not by the entry count

- **WHEN** `Record` is called with a `capacity` far larger than its entry count
- **THEN** level 0 still dispatches `ceil(capacity / 256)` workgroups
- **AND** every record slot of region 1 is rewritten by that call
- **AND** the deeper levels dispatch for their own `R_i`, which are derived from the capacity

#### Scenario: Single level for a small array

- **WHEN** `Record` is called with a capacity at or below 256
- **THEN** exactly one compute dispatch is recorded
- **AND** every run is complete within that single block, so no record is carried to another level

#### Scenario: An entry count below the capacity ignores everything above it

- **WHEN** `Record` is called with an entry count well below the capacity
- **AND** the pair array and the value buffer above the entry count hold garbage, including entries whose keys are valid and whose payload slots are valid
- **THEN** the per-key sums equal those of a reduction over the first `entry count` entries only
- **AND** no value at or above the entry count is read

#### Scenario: An entry count equal to the capacity is the identity

- **WHEN** `Record` is called with an entry count equal to the capacity
- **THEN** the result is identical to the result of the same call with the bound absent
- **AND** every element's values are gathered from the value buffer exactly as before

#### Scenario: A zero entry count produces no output

- **WHEN** `Record` is called with an entry count of zero
- **THEN** level 0 is still dispatched over `ceil(capacity / 256)` workgroups
- **AND** no value is read from the value buffer
- **AND** no output slot is written

#### Scenario: Level 0 gathers values by payload slot

- **WHEN** `Record` is called with a sorted pair array whose entries carry slots in a scrambled order (e.g. `[(0,3), (0,1), (5,0)]`) and an entry count of 3
- **AND** the value buffer holds `values[0..2] = 1.0, 2.0, 4.0` at slots 0, 1 and 3 respectively
- **THEN** `out[0]` equals `2.0 + 1.0` (slots 1 and 3, in either order)
- **AND** `out[5]` equals `4.0` (slot 0)
- **AND** the result does not depend on the order the pairs appear in

#### Scenario: Invalid arguments are rejected per call

- **WHEN** `Record` is called with `num_channels` outside `[1, 8]`, with a zero capacity, or with a zero `max_key_value`
- **THEN** a `std::invalid_argument` exception is thrown

### Requirement: Recursive segmented reduction correctness

The reduction SHALL produce, for every key `b < max_key_value` present within the first `entry count` elements, `out[b] = sum of the values of all entries with key b`, in exact per-channel order. Entries whose key is greater than or equal to `max_key_value` (including the `0xFFFFF` INVALID sentinel) SHALL be ignored and SHALL NOT write to the output. Keys absent from those entries SHALL leave their output slots untouched.

The algorithm SHALL require the keys of the elements below the entry count to be sorted in ascending order. Relative order of entries within the same key SHALL not affect the per-key result set (sums of the same value set; floating-point rounding may vary with order).

Each block SHALL process 256 entries: load into shared memory, perform same-key pairwise doubling with dead marking, write per-key sums for segments fully contained in the block directly to the output, and emit at most two boundary records per block (first segment partial, last segment partial; the second record SHALL be an all-zero record when the whole block belongs to one segment).

A level's run structure — which block emits which record, and whether a record's run is complete — SHALL be derived from the level element count, never from the entry count. Elements at or beyond the entry count keep their keys (with zeroed values) so that they participate in run classification exactly like any other element; their keys SHALL be such that they never write to the output.

#### Scenario: Out-of-range payload slot contributes nothing

- **WHEN** the sorted pair array contains the entry `(7, 999)` while the capacity is 256
- **THEN** no value is read at slot 999
- **AND** that entry adds 0.0 to every channel of key 7
- **AND** every other key's sums are unaffected

#### Scenario: Single key spanning many blocks

- **WHEN** the sorted input contains 10000 entries all with key 5 and values all 1.0, with an entry count of 10000
- **THEN** `out[5]` equals 10000.0 after `Record` completes

#### Scenario: Many keys within one block

- **WHEN** the sorted input contains 100 entries with keys 0..99 each appearing once with value 2.0, with an entry count of 100
- **THEN** `out[b]` equals 2.0 for every `b` in 0..99

#### Scenario: INVALID sentinel entries are ignored

- **WHEN** the entries below the entry count contain valid entries plus a trailing run of entries with key `0xFFFFF` (greater than `max_key_value`)
- **THEN** no output slot outside `[0, max_key_value)` is written
- **AND** all valid per-key sums are unaffected

#### Scenario: Absent keys keep their output untouched

- **WHEN** no entry below the entry count has key 3
- **THEN** `out[3]` retains whatever value it held before `Record`

#### Scenario: An entry count below the capacity leaves a long inert tail

- **WHEN** a call's entry count covers fewer than two blocks while its capacity covers many
- **AND** every entry at or beyond the entry count carries the `0xFFFFF` key with an arbitrary value
- **THEN** the per-key sums equal those of a reduction over the entries below the entry count
- **AND** the tail's records and partial sums never reach the output

#### Scenario: A smaller entry count after a larger one leaves no residue

- **WHEN** a call with a small entry count follows a call that used a larger entry count on the same buffers
- **THEN** the final per-key sums are unaffected by the earlier call's records

### Requirement: N-channel batched values

The value buffer SHALL store `num_channels` float channels in channel-major layout: the value of channel `c` at element `i` SHALL reside at `values[c * stride + i]`. At level 0 the element index is the record's payload slot and `stride` is the call's **capacity**; at levels >= 1 the element index is the record's position and `stride` is that level's record count `R_i`. The output buffer SHALL use the same channel-major layout with stride `max_key_value`.

The channel count SHALL be passed to the shader via the push-constant block at record time; a single `Record` call SHALL reduce all channels with one pass per level (no per-channel dispatches).

#### Scenario: Seven channels reduce in one pass per level

- **WHEN** `Record` is called with `num_channels = 7` over sorted entries
- **THEN** each of the 7 per-key sums is written to its channel region of the output buffer
- **AND** the number of dispatches equals the level count, independent of `num_channels`

#### Scenario: Level-0 stride is the capacity, not the entry count

- **WHEN** `Record` is called with a capacity larger than its entry count
- **THEN** each entry's values are gathered from `values[c * capacity + slot]`
- **AND** the gathered values do not depend on the entry count
