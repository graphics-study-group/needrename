# gpu-sum-by-key

## MODIFIED Requirements

### Requirement: SumByKey class construction

The `SumByKey` class SHALL be constructible with `(Rhi::DeviceContext &device_context)` alone. Element geometry SHALL NOT be a construction-time parameter and the instance SHALL hold no geometry state. The constructor SHALL NOT allocate any GPU resources; shader loading and kernel acquisition SHALL be deferred until the first `Record` call.

The class SHALL reside in `engine/Physics/gpu_algorithm/` and SHALL NOT depend on any detector, solver, or collision-specific types. Dependencies SHALL be limited to the Rhi compute infrastructure (`ComputeBuffer`, the compute kernel facility, `DeviceContext`) plus `vk::CommandBuffer`.

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

#### Scenario: No shader-module bookkeeping is exposed

- **WHEN** the `SumByKey` class is inspected for the shader pipeline it uses
- **THEN** it holds no per-instance shader stage or binding object
- **AND** its shader is obtained from the device-level kernel facility on demand

#### Scenario: The count source selects the kernel

- **WHEN** a call's entry count is a CPU-known value
- **THEN** the call is recorded with the value-count kernel, which binds no entry-count buffer
- **WHEN** a call's entry count is produced on the GPU
- **THEN** the call is recorded with the buffer-count kernel and binds the entry-count buffer

### Requirement: Record dispatches the recursive level chain

`Record` SHALL accept the sorted key array, the payload-index array, the packed value buffer, the record buffer, the output buffer, and an **entry-count source**, and SHALL dispatch exactly `k` compute passes (one per level) with a full compute barrier between consecutive levels:

- Level 0 SHALL read the caller's sorted key array and payload-index array, take each entry's key from `keys_in[e]`, gather each of its `num_channels` values from `values[c * capacity + payload_in[e]]`, and SHALL write boundary records into record region 1.
- Levels `1 .. k-2` SHALL read record region `i` and SHALL write record region `i+1`. At these levels the key of record `e` SHALL be `keys_in[e]` and channel `c` SHALL be `values_in[c * R_i + e]`.
- Level `k-1` (a single workgroup covering `R_{k-1} <= 256` records) SHALL write all remaining per-key sums directly to the output buffer.

**Two different lengths bound a level, and they SHALL NOT be conflated.**

The **level element count** is the element count of the array a level reads: the call's `capacity` at level 0 (`R_0 = capacity`), and `R_i` at level `i >= 1`. It alone SHALL determine:

- the element-index bound (`e < level element count`), which decides whether an element exists at all;
- the per-block real-extent used for run classification, and therefore which boundary records a block emits;
- the per-level workgroup count — level 0 SHALL always dispatch `ceil(capacity / 256)` workgroups so that every record slot of region 1 is rewritten on every call;
- the record-region partition (`R_{i+1} = 2 * ceil(R_i / 256)` records, one pair of slots per block);
- the level-0 channel stride of the value buffer.

The **entry count** is the number of leading elements that carry real input data. It SHALL bound **data extent only**: at level 0 no value SHALL be read for an element at or beyond it, and it SHALL have no effect on the element-index bound, on run classification, on the workgroup count, or on the record-region layout. A call whose entry count equals the call's capacity SHALL behave exactly as if the bound were absent.

**The entry count SHALL be supplied through a count source, which is either a CPU-known value or a GPU-produced value.** A GPU-produced count SHALL be read from the caller's entry-count buffer inside the shader at execution time, because the producing pass runs on the GPU and the host does not know the value at record time. A CPU-known count SHALL travel in the push-constant block. The source SHALL select the shader variant used for the call, and a variant SHALL declare only the descriptor bindings it actually binds: the buffer-count variant binds the entry-count buffer at every level, and the value-count variant binds no such buffer. A caller whose count is a CPU-known value equal to the call's capacity passes the value form and gets the identity behaviour described above.

Because the level element count and not the entry count sizes level 0, every block of level 0 always writes its portion of region 1, and the record chain stays dense: each of the `2 * ceil(capacity / 256)` slots of region 1 is written on every call. Sizing level 0's workgroup count by the entry count would leave record slots from an earlier call in place, which the next level reads as phantom partials.

Elements at or beyond the entry count SHALL NOT have their **values** read. This includes the caller's key and payload-index arrays: the values of an entry whose index is at or beyond the entry count SHALL NOT be gathered from the value buffer, and neither SHALL the values of an entry whose payload index is at or beyond it. Such an entry SHALL still contribute its key to the level's run structure and SHALL contribute 0.0 to every channel. Callers therefore only need valid values below the entry count, and only need the key and payload-index arrays themselves to be writable up to their bound (the entry passes' full-capacity write contract covers that).

An entry whose payload index is outside `[0, capacity)` SHALL contribute 0.0 to every channel and SHALL NOT be read from the value buffer, so a caller that emits a bad payload index loses that entry instead of triggering a wild read. Its key SHALL be used unchanged: rewriting the key would violate the ascending-order requirement below and could split a real key's run, losing one of its partial sums.

`num_channels` SHALL be in `[1, 8]` and `capacity` SHALL be greater than zero; a call violating either, or a `max_key_value` of zero, SHALL throw `std::invalid_argument`. `Record` SHALL additionally reject a capacity whose implied buffer sizes exceed the buffers bound for that call.

The input mode SHALL travel in the push-constant block as a `gather_payload` flag that `Record` sets for level 0 and clears for every deeper level. In gather mode the level's input SHALL always be the caller's key array plus its payload-index array, and the entry-count bound SHALL apply to both. In non-gather mode the level's input SHALL always be the `KeysIn` / `ValuesIn` key and value arrays, and no entry-count bound SHALL apply, because a deeper-level input is produced by the previous level and is entirely valid.

All other per-level parameters (region offsets, level element count, workgroup count, channel count, key bound) SHALL also travel via the shader's push-constant block. The caller SHALL insert the outer barriers around the whole `Record`.

The shader's storage-buffer bindings SHALL be `KeysIn`, `PayloadIn`, `ValuesIn`, `RecKeys`, `RecValues`, `OutValues` — six buffers — plus the entry-count buffer in the buffer-count variant only. There SHALL be no `uvec2` pair binding: level 0's key and payload index come from the same two scalar arrays every other level uses.

#### Scenario: Full recursion for a large array

- **WHEN** `Record` is called with `capacity = 200000` (`k = 3`) and an entry count equal to the capacity
- **THEN** three compute dispatches are recorded (782, 7, and 1 workgroups respectively)
- **AND** a barrier is recorded between consecutive dispatches
- **AND** only the first dispatch reads `PayloadIn` (`gather_payload = 1`)

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
- **AND** the key array, the payload-index array and the value buffer above the entry count hold garbage, including entries whose keys are valid and whose payload indices are valid
- **THEN** the per-key sums equal those of a reduction over the first `entry count` entries only
- **AND** no value at or above the entry count is read

#### Scenario: An entry count equal to the capacity is the identity

- **WHEN** `Record` is called with an entry count equal to the capacity
- **THEN** the result is identical to the result of the same call with the bound absent

#### Scenario: A zero entry count produces no output

- **WHEN** `Record` is called with an entry count of zero
- **THEN** level 0 is still dispatched over `ceil(capacity / 256)` workgroups
- **AND** no value is read from the value buffer
- **AND** no output slot is written

#### Scenario: Level 0 gathers values by payload index

- **WHEN** `Record` is called with sorted keys `[0, 0, 5]` and payload indices `[3, 1, 0]`, and an entry count of 3
- **AND** the value buffer holds `1.0`, `2.0` and `4.0` at payload slots 3, 1 and 0 respectively
- **THEN** `out[0]` equals `1.0 + 2.0` (in either order)
- **AND** `out[5]` equals `4.0`
- **AND** the result does not depend on the order the entries appear in

#### Scenario: Invalid arguments are rejected per call

- **WHEN** `Record` is called with `num_channels` outside `[1, 8]`, with a zero capacity, or with a zero `max_key_value`
- **THEN** a `std::invalid_argument` exception is thrown

#### Scenario: A CPU-known count needs no count buffer

- **WHEN** `Record` is called with a count source that is a CPU-known value
- **THEN** the count travels in a push-constant block
- **AND** no entry-count buffer is bound for that call
- **AND** the variant's declared bindings are exactly the buffers it binds

#### Scenario: A CPU-known count equal to the capacity matches the unbound case

- **WHEN** `Record` is called with a count source that is a CPU-known value equal to the capacity
- **THEN** the result equals the same call made with the buffer-count source
- **AND** neither call's result differs from the bound being absent

## ADDED Requirements

### Requirement: Temporary storage is reused across calls and its barrier is the caller's

The temporary storage a reduction reads and writes SHALL be sized for the largest geometry it is recorded against and SHALL be reused across calls rather than reallocated per call. Storage SHALL grow when a call's geometry exceeds what it holds, SHALL NOT shrink when a later call's geometry is smaller, and SHALL NOT be reallocated by a call that fits.

Because two recordings that share one storage set have an execution-order dependency through it, the caller SHALL record the barrier that dependency requires between consecutive recordings that reuse the same storage on the same instance. Recordings that use **different** storage sets SHALL be independent for storage reasons, so a caller that wants two reductions interleaved without a storage barrier supplies two storage sets and accepts the duplication.

#### Scenario: Repeated calls at the same geometry reuse the storage

- **WHEN** `Record` is called repeatedly with the same capacity
- **THEN** only the first call sizes the storage
- **AND** subsequent calls reuse it and allocate nothing

#### Scenario: A capacity increase grows the storage once

- **WHEN** a call's capacity exceeds the storage's current capacity
- **THEN** that call grows the storage
- **AND** calls up to the new capacity perform no further allocation

#### Scenario: A capacity decrease reuses the storage

- **WHEN** a call's capacity is smaller than the storage's current capacity
- **THEN** the call succeeds without allocating
- **AND** the storage is not shrunk

#### Scenario: Two storage sets are independent

- **WHEN** two reductions are recorded with distinct storage sets on the same instance
- **THEN** neither reduction's storage is read or written by the other's dispatches
- **AND** no barrier between the two recordings is required for storage reasons
